#!/usr/bin/env python3
"""Redige dados pessoais de fixtures HTML/HAR do SIGAA antes do commit.

Motivação em docs/RECON.md §4. O HTML do portal contém, em texto puro:
  - nome completo do aluno (query string dos links de chat)
  - CPF (campo user.login do form de login)
  - matrícula, e-mail institucional, idusuario
  - JSESSIONID, e o `key` da foto de perfil

Uso:
    python tools/redact.py entrada.html -o tests/fixtures/portal.html
    python tools/redact.py --check tests/fixtures/*.html   # falha se achar PII

O --check serve de hook de pre-commit: sai com código 1 se sobrou PII.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# Cada regra é (nome, padrão, substituição). A ordem importa: as mais
# específicas primeiro, para não serem canibalizadas pelas genéricas.
RULES: list[tuple[str, re.Pattern[str], str]] = [
    # Campo de login do SIGAA: <input name="user.login" value="<CPF>">.
    # Precisa vir antes de cpf-solto para preservar o formato sem pontuação,
    # que é o que o servidor realmente recebe.
    ("cpf-input",
     re.compile(r"""(name=["']user\.login["'][^>]*?value=["'])\d{11}""", re.IGNORECASE),
     r"\g<1>00000000000"),
    ("cpf-campo",
     re.compile(r"(user\.login[\"']?\s*[:=]\s*[\"']?)\d{11}"),
     r"\g<1>00000000000"),
    # Placeholder sem pontuação, igual ao de cpf-input: as regras rodam em
    # cascata sobre o mesmo texto, então o resultado de uma tem que ser um
    # ponto fixo das seguintes. Com "000.000.000-00" aqui, esta regra casaria
    # o placeholder de cpf-input e o reescreveria — corrompendo o fixture.
    ("cpf-solto",
     re.compile(r"\b\d{3}\.?\d{3}\.?\d{3}-?\d{2}\b"),
     "00000000000"),
    ("jsessionid",
     re.compile(r"(JSESSIONID=)[A-Za-z0-9._-]+"),
     r"\g<1>REDACTED_SESSION"),
    ("foto-key",
     re.compile(r"(idFoto=)\d+(&(?:amp;)?key=)[0-9a-f]+"),
     r"\g<1>0\g<2>REDACTED"),
    ("idusuario",
     re.compile(r"(idusuario=)\d+"),
     r"\g<1>00000"),
    # O mesmo id aparece como argumento posicional de JS, fora de query string:
    #   exibirJanelaVideoChat(&quot;88080&quot;, 25318, &quot;db86...&quot;)
    # Dois cuidados: o nome real da funcao e "exibirJanelaVideoChat" (V
    # maiusculo — casar "videoChat" sem IGNORECASE nao pega), e o terceiro
    # argumento e um token de chat que tambem nao deve ir para o repo.
    ("idusuario-js",
     re.compile(r"(VideoChat\(\s*(?:&quot;|[\"'])\d+(?:&quot;|[\"'])\s*,\s*)\d+",
                re.IGNORECASE),
     r"\g<1>00000"),
    ("chat-token",
     re.compile(r"(VideoChat\([^)]*?,\s*\d+\s*,\s*(?:&quot;|[\"']))[0-9a-f]{8,}",
                re.IGNORECASE),
     r"\g<1>REDACTED"),
    ("nome-em-url",
     re.compile(r"(nomeUsuario=)[^&'\"]+"),
     r"\g<1>ALUNO+TESTE"),
    # TLD opcional de proposito: no HTML do SIGAA o e-mail aparece truncado
    # ("d2023013362@unifei....") e as vezes quebrado por tags, entao exigir
    # dominio completo faz a regra nunca casar. "@media" e afins nao casam
    # porque nao ha parte local antes do @.
    ("email",
     re.compile(r"\b[\w.+-]+@[\w-]+(?:\.[\w-]+)*"),
     "aluno@example.edu"),
    # Matricula da UNIFEI tem 10 digitos (ex.: 20 + 8). A versao anterior
    # exigia 11 e por isso nunca casou — vazou em fixture real.
    ("matricula",
     re.compile(r"\b20\d{8,9}\b"),
     "2000000000"),
]

# Nomes próprios não têm padrão sintático — precisam ser informados.
def name_rules(names: list[str]) -> list[tuple[str, re.Pattern[str], str]]:
    """Gera regras para o nome completo E para cada token isolado.

    Casar só o nome completo não basta: o SIGAA exibe o mesmo nome em várias
    formas na mesma página — completo nos links de chat
    ("MATHEUS FELIPE SILVA COELHO") e abreviado no cabeçalho
    ("MATHEUS FELIPE S. COELHO"). Enumerar variantes de abreviação é
    inviável; redigir token a token cobre qualquer arranjo.

    Redige a mais que o necessário (um sobrenome comum pode aparecer noutro
    contexto), o que é o lado certo para errar num redator de PII.
    """
    out: list[tuple[str, re.Pattern[str], str]] = []
    for n in names:
        # 1) nome completo, inclusive URL-encoded com '+'
        completo = "|".join([re.escape(n), re.escape(n.replace(" ", "+"))])
        out.append((f"nome:{n}", re.compile(completo, re.IGNORECASE), "ALUNO TESTE"))

        # 2) cada token com 3+ letras, isolado por limite de palavra
        tokens = [t for t in re.split(r"[\s.]+", n) if len(t) >= 3]
        for t in tokens:
            out.append((
                f"nome-token:{t}",
                re.compile(rf"\b{re.escape(t)}\b", re.IGNORECASE),
                "ALUNO",
            ))
    return out


def read_text_any(path: pathlib.Path) -> tuple[str, str]:
    """Lê o arquivo detectando o encoding, e devolve (texto, encoding_usado).

    O SIGAA serve **windows-1252** (<meta charset=windows-1252>). Ler com
    encoding='utf-8', errors='replace' — o que este script fazia antes —
    troca cada byte acentuado por U+FFFD ANTES da redação, e a gravação em
    UTF-8 sela a perda: 'EQUAÇÕES' vira 'EQUA\\ufffd\\ufffdES' e nenhum
    parser recupera. O fixture nasce corrompido.
    """
    raw = path.read_bytes()
    m = re.search(rb"charset=[\"']?\s*([\w-]+)", raw[:4096], re.IGNORECASE)
    declarado = m.group(1).decode("ascii", "ignore").lower() if m else None

    for cand in (declarado, "utf-8", "cp1252"):
        if not cand:
            continue
        try:
            return raw.decode(cand), cand
        except (UnicodeDecodeError, LookupError):
            continue
    # cp1252 mapeia todos os 256 bytes, então este caminho é quase inalcançável
    return raw.decode("cp1252", errors="replace"), "cp1252"


# Depois de normalizar para UTF-8, o <meta charset> antigo passa a mentir.
META_CHARSET = re.compile(rb"(charset=[\"']?\s*)(windows-1252|iso-8859-1)", re.IGNORECASE)


def redact(text: str, rules) -> tuple[str, dict[str, int]]:
    """Aplica as regras em cascata.

    `hits` conta apenas substituições que REALMENTE alteraram o texto. Como os
    placeholders são ponto fixo das regras (ver cpf-solto), reaplicar sobre um
    arquivo já redigido casa os padrões mas não muda nada — e isso não pode
    contar como PII, senão o --check acusa arquivo limpo.
    """
    hits: dict[str, int] = {}
    for label, pat, repl in rules:
        new, n = pat.subn(repl, text)
        if n and new != text:
            hits[label] = hits.get(label, 0) + n
        text = new
    return text, hits


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", type=pathlib.Path)
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    help="destino (só com um arquivo de entrada)")
    ap.add_argument("--name", action="append", default=[],
                    help="nome próprio a redigir; pode repetir")
    ap.add_argument("--check", action="store_true",
                    help="não escreve; sai 1 se encontrar PII")
    args = ap.parse_args()

    if args.output and len(args.files) > 1:
        ap.error("-o só funciona com um arquivo de entrada")

    rules = RULES + name_rules(args.name)
    dirty = False

    for path in args.files:
        try:
            original, enc = read_text_any(path)
        except OSError as e:
            print(f"erro lendo {path}: {e}", file=sys.stderr)
            return 1

        cleaned, hits = redact(original, rules)

        if args.check:
            if hits:
                dirty = True
                achados = ", ".join(f"{k}x{v}" for k, v in sorted(hits.items()))
                print(f"PII ENCONTRADA {path}: {achados}", file=sys.stderr)
            continue

        dest = args.output or path
        dest.parent.mkdir(parents=True, exist_ok=True)
        # Grava sempre UTF-8 e corrige o <meta charset>, que passaria a mentir.
        dest.write_bytes(META_CHARSET.sub(rb"\g<1>utf-8", cleaned.encode("utf-8")))

        resumo = ", ".join(f"{k}x{v}" for k, v in sorted(hits.items())) or "nada encontrado"
        origem = "" if enc in ("utf-8", "utf8") else f", {enc} -> utf-8"
        print(f"{path} -> {dest}  ({resumo}{origem})")

    if args.check and dirty:
        print("\nfalhou: redija com tools/redact.py antes de commitar.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
