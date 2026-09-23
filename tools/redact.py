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
    # Id interno da pessoa, no A4J de "Visualizar Perfil" da aba Participantes:
    #   'parameters':{'idPessoa':194699, ...}
    # E o mesmo tipo de identificador que idusuario, so que noutra tela.
    ("idpessoa",
     re.compile(r"(['\"]idPessoa['\"]\s*:\s*)\d+"),
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
    # O nome do DONO DA CONTA, no 5o argumento de exibirJanelaVideoChat.
    #
    # Existe porque `name_rules` so roda com --name, e --name depende de
    # alguem lembrar. Nao lembraram: `arquivos_rede.html` e `arquivos_vazio.html`
    # foram commitados com o nome completo do aluno em texto puro, e o --check
    # deu verde. Regra estrutural nao tem esse modo de falha — funciona em
    # fixture de qualquer conta, sem flag.
    #
    # Ancorada no `, false` que SEGUE o nome: e o unico argumento da chamada
    # seguido de um booleano, entao o `[^)]*?` preguicoso nao tem como parar
    # no argumento errado.
    ("nome-em-videochat",
     re.compile(r"(VideoChat\([^)]*?(?:&quot;|[\"']))"
                r"([^\"'&]+?)"
                r"((?:&quot;|[\"'])\s*,\s*(?:false|true))",
                re.IGNORECASE),
     r"\g<1>ALUNO TESTE\g<3>"),
    # O mesmo nome, agora no cabecalho de toda pagina logada: o SIGAA escreve
    # o dono da conta solto dentro de #painelDadosUsuario. Estrutural pelo
    # mesmo motivo da regra acima — nao pode depender de --name.
    # Os \s* ficam DENTRO dos grupos 1 e 3, e nao fora: engolir o espaco em
    # volta do nome faria a regra reescrever o arquivo mesmo quando ele ja esta
    # redigido — e o --check, que conta so as regras que MUDARAM o texto,
    # acusaria de PII um fixture limpo. Toda regra daqui tem de ser ponto fixo
    # de si mesma (ver o docstring de redact()).
    ("nome-no-cabecalho",
     re.compile(r"(id=[\"']painelDadosUsuario[\"'][\s\S]{0,400}?<p[^>]*>\s*)"
                r"([^<>\s][^<>]*?)"
                r"(\s*</p>)"),
     r"\g<1>ALUNO TESTE\g<3>"),
    # --- aba Participantes ------------------------------------------------
    # Aqui os nomes nao sao UM (o do dono da conta), sao a TURMA INTEIRA: 31
    # colegas mais o professor, cada um com CPF, matricula e e-mail ao lado.
    # `name_rules` nao serve — exigiria um --name por pessoa, e um esquecido
    # vaza. Estas duas regras redigem por ESTRUTURA, entao funcionam numa turma
    # que nunca vimos.
    #
    # Discente: o nome e o texto solto logo apos <strong>, em caixa alta.
    # O primeiro caractere tem que ser do nome (e nao \s) senao a regra tambem
    # casaria o <strong> do docente, que abre com <a>, e injetaria texto antes
    # do link — corrompendo justamente o no de onde o parser le o nome dele.
    # Os &#NNN; sao obrigatorios na classe: o SIGAA escreve "JO&#195;O" e
    # "VIN&#205;CIUS", e sem eles tres nomes de 32 passavam limpos.
    ("nome-participante",
     re.compile(r"(<strong>\s*)"
                r"((?:[A-ZÀ-Ü.'-]|&#\d+;)(?:(?:[A-ZÀ-Ü.'-]|&#\d+;)|[ \t]){4,}?)"
                r"(?=\s*<)"),
     r"\g<1>ALUNO TESTE"),
    # Docente: o nome e o texto do link para a pagina publica dele. O CPF que
    # vai no ?login= da mesma URL cai na regra cpf-solto.
    ("nome-docente",
     re.compile(r"(RedirectDocente[^>]*>)[^<]+(</a>)"),
     r"\g<1>DOCENTE TESTE\g<2>"),
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


# Caminho padrao do .env de quem esta testando. Fica em tests/fixtures/ e nao
# na raiz porque e um arquivo de FIXTURE: quem gera fixture e quem precisa
# dele, e quem so compila o projeto nunca o vera.
ENV_PADRAO = pathlib.Path(__file__).resolve().parent.parent / "tests" / "fixtures" / ".env"

# Chave -> se o valor e um NOME (sem padrao sintatico, precisa de name_rules)
# ou um literal qualquer (redigido como texto exato).
CHAVES_NOME = ("SIGAA_NOME", "SIGAA_NOMES_EXTRA")
CHAVES_LITERAL = {
    "SIGAA_EMAIL": "aluno@example.edu",
    "SIGAA_CPF": "00000000000",
    "SIGAA_MATRICULA": "2000000000",
    "SIGAA_IDUSUARIO": "00000",
}


def carregar_env(path: pathlib.Path) -> dict[str, str]:
    """Le o .env de quem esta testando. Ausente = dict vazio, sem erro.

    NAO e obrigatorio de proposito: as regras estruturais de RULES cobrem CPF,
    e-mail, matricula e os dois lugares onde o SIGAA escreve o nome do dono da
    conta. O .env existe para o que sobra — um apelido, um nome do meio escrito
    diferente, o nome de um colega que aparece numa mensagem. Exigi-lo faria o
    --check falhar em CI, que nao tem .env nenhum.
    """
    if not path.is_file():
        return {}
    out: dict[str, str] = {}
    for linha in path.read_text(encoding="utf-8", errors="replace").splitlines():
        linha = linha.strip()
        if not linha or linha.startswith("#") or "=" not in linha:
            continue
        chave, _, valor = linha.partition("=")
        valor = valor.strip().strip('"').strip("'")
        if valor:
            out[chave.strip()] = valor
    return out


def regras_do_env(env: dict[str, str]) -> list[tuple[str, re.Pattern[str], str]]:
    """Converte o .env em regras.

    Nomes viram `name_rules` (que quebra em tokens, porque o SIGAA abrevia o
    mesmo nome de formas diferentes na mesma pagina). O resto vira literal
    exato — redundante com as regras estruturais no caso normal, e a rede de
    seguranca quando o SIGAA escreve o dado num formato que elas nao preveem.
    """
    regras: list[tuple[str, re.Pattern[str], str]] = []

    nomes: list[str] = []
    for chave in CHAVES_NOME:
        # Varios nomes por chave, separados por ";" — o caso de quem testa com
        # mais de uma conta, ou precisa redigir um colega citado numa mensagem.
        nomes += [n.strip() for n in env.get(chave, "").split(";") if n.strip()]
    regras += name_rules(nomes)

    for chave, placeholder in CHAVES_LITERAL.items():
        valor = env.get(chave, "")
        if not valor or valor == placeholder:
            continue   # nao redigir o placeholder para ele mesmo: nao seria ponto fixo
        regras.append((f"env:{chave.lower()}",
                       re.compile(re.escape(valor), re.IGNORECASE),
                       placeholder))
    return regras


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
    ap.add_argument("--env", type=pathlib.Path, default=ENV_PADRAO,
                    help=f"dados de quem testa (padrão: {ENV_PADRAO})")
    ap.add_argument("--sem-env", action="store_true",
                    help="ignora o .env; use para conferir só as regras estruturais")
    args = ap.parse_args()

    if args.output and len(args.files) > 1:
        ap.error("-o só funciona com um arquivo de entrada")

    env = {} if args.sem_env else carregar_env(args.env)
    # Ordem: estruturais, depois .env. As do .env sao literais e nao dependem
    # do contexto, entao rodar por ultimo nao muda o resultado — mas deixa o
    # relatorio de hits mais legivel, com o que veio do arquivo no fim.
    rules = RULES + name_rules(args.name) + regras_do_env(env)
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
