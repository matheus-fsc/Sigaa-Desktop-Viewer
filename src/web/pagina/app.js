// SIGAA Viewer — acesso mobile. Sem framework e sem build: o servidor embute
// este arquivo como está (cmake/Embutir.cmake).
//
// REGRA DESTE ARQUIVO: nenhum dado vira HTML. Título de tópico, notícia e nome
// de arquivo são texto do professor; tudo entra por `textContent` via h(), e
// não existe innerHTML aqui. A CSP do servidor fecha a porta de novo, mas a
// primeira tranca é esta.
'use strict';

(() => {
  const CHAVE_TOKEN = 'sigaa.token';
  const CHAVE_TEMA = 'sigaa.tema';

  // --- armazenamento: pode faltar (aba anônima, Safari com bloqueio) -------
  const guardado = {
    ler(k) { try { return localStorage.getItem(k); } catch { return null; } },
    gravar(k, v) { try { localStorage.setItem(k, v); } catch { /* sem memória */ } },
    apagar(k) { try { localStorage.removeItem(k); } catch { /* idem */ } },
  };
  let tokenEmMemoria = null;
  const token = () => tokenEmMemoria || guardado.ler(CHAVE_TOKEN);
  const guardarToken = (t) => { tokenEmMemoria = t; guardado.gravar(CHAVE_TOKEN, t); };

  // --- DOM -----------------------------------------------------------------
  const $ = (id) => document.getElementById(id);

  // h('div', {class: 'x', onclick: f}, 'texto', outroNo, [lista], null)
  function h(tag, props, ...filhos) {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(props || {})) {
      if (v == null || v === false) continue;
      if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
      else if (k === 'class') el.className = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
    const por = (f) => {
      if (f == null || f === false) return;
      if (Array.isArray(f)) f.forEach(por);
      else el.append(f instanceof Node ? f : document.createTextNode(String(f)));
    };
    filhos.forEach(por);
    return el;
  }

  function mostrar(...nos) {
    const m = $('conteudo');
    m.replaceChildren(...nos);
    m.scrollTop = 0;
  }

  // --- datas -----------------------------------------------------------------
  const DS = ['dom', 'seg', 'ter', 'qua', 'qui', 'sex', 'sáb'];
  const MES = ['jan', 'fev', 'mar', 'abr', 'mai', 'jun', 'jul', 'ago', 'set', 'out', 'nov', 'dez'];

  // "2026-10-01" ou "2026-10-01T10:00", sempre hora local — é o que o SIGAA dá.
  function lerData(s) {
    if (!s) return null;
    const [d, t] = s.split('T');
    const [a, m, dia] = d.split('-').map(Number);
    const [hh, mm] = t ? t.split(':').map(Number) : [0, 0];
    return new Date(a, m - 1, dia, hh, mm);
  }
  const dd = (n) => String(n).padStart(2, '0');
  const diaMes = (d) => `${dd(d.getDate())}/${dd(d.getMonth() + 1)}`;
  const diaCurto = (d) => `${DS[d.getDay()]} ${diaMes(d)}`;
  const hora = (s) => (s && s.includes('T') ? s.split('T')[1].slice(0, 5) : null);
  const isoDia = (d) => `${d.getFullYear()}-${dd(d.getMonth() + 1)}-${dd(d.getDate())}`;

  function relativo(n) {
    if (n === 0) return 'hoje';
    if (n === 1) return 'amanhã';
    if (n === -1) return 'ontem';
    return n > 0 ? `em ${n} dias` : `há ${-n} dias`;
  }
  function relSemana(n) {
    if (n === 0) return 'esta semana';
    if (n === 1) return 'próxima semana';
    if (n === -1) return 'semana passada';
    return n > 0 ? `daqui a ${n} semanas` : `há ${-n} semanas`;
  }
  function haQuanto(epoch) {
    const min = Math.round((Date.now() / 1000 - epoch) / 60);
    if (min < 2) return 'agora há pouco';
    if (min < 60) return `há ${min} min`;
    const horas = Math.round(min / 60);
    if (horas < 24) return `há ${horas} h`;
    const dias = Math.round(horas / 24);
    return dias === 1 ? 'ontem' : `há ${dias} dias`;
  }
  function segunda(d) {
    const x = new Date(d.getFullYear(), d.getMonth(), d.getDate());
    x.setDate(x.getDate() - ((x.getDay() + 6) % 7));
    return x;
  }

  // Só a etiqueta do que exige ação: atrasado, hoje, amanhã. "Em 8 dias" é
  // texto simples — etiqueta em toda linha não é hierarquia (README, Agenda).
  function etiquetaPrazo(n) {
    if (n < 0) return h('span', { class: 'pilula perigo' }, `! ${n === -1 ? 'venceu ontem' : `atrasado ${-n} dias`}`);
    if (n === 0) return h('span', { class: 'pilula aviso' }, '◷ vence hoje');
    if (n === 1) return h('span', { class: 'pilula aviso' }, '◷ amanhã');
    return h('span', null, relativo(n));
  }

  // --- API -------------------------------------------------------------------
  class SemPareamento extends Error {}

  async function api(caminho) {
    const t = token();
    if (!t) throw new SemPareamento();
    let r;
    try {
      r = await fetch(caminho, { headers: { Authorization: `Bearer ${t}` }, cache: 'no-store' });
    } catch {
      throw new Error('O computador não respondeu. Ele está ligado, com o app aberto e a VPN conectada?');
    }
    if (r.status === 401) throw new SemPareamento();
    const corpo = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(corpo.erro || `Erro ${r.status}`);
    return corpo;
  }

  async function baixar(idTurma, arquivo, botao) {
    botao.disabled = true;
    const antes = botao.textContent;
    botao.textContent = 'Baixando…';
    try {
      const r = await fetch(`/api/arquivos/${idTurma}/${arquivo.id}`, {
        headers: { Authorization: `Bearer ${token()}` }, cache: 'no-store',
      });
      if (r.status === 401) throw new SemPareamento();
      if (!r.ok) throw new Error((await r.json().catch(() => ({}))).erro || `Erro ${r.status}`);
      const blob = await r.blob();
      const url = URL.createObjectURL(blob);
      const nome = nomeDoArquivo(r.headers.get('Content-Disposition')) || arquivo.titulo;
      const a = h('a', { href: url, download: nome });
      document.body.append(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 60000);
      botao.textContent = antes;
    } catch (e) {
      if (e instanceof SemPareamento) return pedirPareamento();
      botao.textContent = 'Falhou';
      estado(e.message);
    } finally {
      botao.disabled = false;
    }
  }
  function nomeDoArquivo(cd) {
    const m = cd && /filename\*=UTF-8''([^;]+)/i.exec(cd);
    try { return m ? decodeURIComponent(m[1]) : null; } catch { return null; }
  }

  // --- moldura -------------------------------------------------------------------
  let resumo = null;

  function estado(texto) { $('estado').textContent = texto; }

  function textoEstado() {
    if (!resumo) return 'Conectando ao computador…';
    const quando = resumo.ultimo_sync ? `atualizado ${haQuanto(resumo.ultimo_sync)}` : 'ainda sem coleta';
    return `Dados do computador · ${quando} · somente leitura`;
  }

  async function carregarResumo() {
    resumo = await api('/api/resumo');
    for (const [k, v] of Object.entries(resumo.contagens || {})) {
      const el = document.querySelector(`[data-contagem="${k}"]`);
      if (el) el.textContent = String(v);
    }
    $('titulo').textContent = resumo.periodo ? `SIGAA Viewer · ${resumo.periodo}` : 'SIGAA Viewer';
    estado(textoEstado());
  }

  function marcarNav(secao) {
    for (const a of document.querySelectorAll('#nav a')) {
      if (a.dataset.secao === secao) a.setAttribute('aria-current', 'page');
      else a.removeAttribute('aria-current');
    }
  }

  function moldura(visivel) {
    $('nav').hidden = !visivel;
    $('recarregar').hidden = !visivel;
  }

  // --- tema ---------------------------------------------------------------------
  function aplicarTema(t) {
    if (t === 'claro') document.documentElement.dataset.theme = 'light';
    else if (t === 'escuro') document.documentElement.dataset.theme = 'dark';
    else delete document.documentElement.dataset.theme;
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.content = getComputedStyle(document.documentElement).getPropertyValue('--surface').trim() || '#222733';
  }

  // --- menu ⋯ ------------------------------------------------------------------
  function abrirMenu() {
    const atual = guardado.ler(CHAVE_TEMA) || 'sistema';
    let veu;
    const fechar = () => veu.remove();
    const opcaoTema = (valor, rotulo) => h('button', {
      class: 'botao pequeno', type: 'button', 'aria-pressed': String(atual === valor),
      onclick: () => { guardado.gravar(CHAVE_TEMA, valor); aplicarTema(valor); fechar(); },
    }, rotulo);
    const folha = h('div', { class: 'folha', role: 'dialog', 'aria-modal': 'true', 'aria-label': 'Opções' },
      h('h2', null, 'Opções'),
      h('span', { class: 'rotulo' }, 'Tema'),
      h('div', { class: 'escolha' }, opcaoTema('sistema', 'Sistema'), opcaoTema('claro', 'Claro'), opcaoTema('escuro', 'Escuro')),
      h('p', { class: 'segundo pequeno' },
        'Somente leitura. Para buscar novidades no SIGAA ou corrigir uma prova, use o app no computador: ',
        'esta tela mostra o que ele já tem.'),
      h('button', {
        class: 'botao', type: 'button',
        onclick: () => { tokenEmMemoria = null; guardado.apagar(CHAVE_TOKEN); fechar(); pedirPareamento(); },
      }, 'Esquecer este celular'),
      h('button', { class: 'botao primario', type: 'button', onclick: () => fechar() }, 'Fechar'));
    veu = h('div', { class: 'veu', onclick: (e) => { if (e.target === veu) fechar(); } }, folha);
    document.addEventListener('keydown', function esc(e) {
      if (e.key === 'Escape') { fechar(); document.removeEventListener('keydown', esc); }
    });
    document.body.append(veu);
    folha.querySelector('button').focus();
  }

  // --- pareamento ---------------------------------------------------------------
  // O nome que aparece na lista "Aparelhos" do computador. Só o sistema e o
  // navegador: o bastante para o aluno reconhecer o celular, sem mandar o
  // user agent inteiro para o banco.
  function nomeDoAparelho() {
    const ua = navigator.userAgent;
    const so = /iPhone/.test(ua) ? 'iPhone' : /iPad/.test(ua) ? 'iPad' : /Android/.test(ua) ? 'Android'
      : /Windows/.test(ua) ? 'Windows' : /Mac OS X/.test(ua) ? 'Mac' : /Linux/.test(ua) ? 'Linux' : 'Aparelho';
    const nav = /EdgA?\//.test(ua) ? 'Edge' : /SamsungBrowser/.test(ua) ? 'Samsung Internet'
      : /Firefox|FxiOS/.test(ua) ? 'Firefox' : /CriOS|Chrome/.test(ua) ? 'Chrome'
      : /Safari/.test(ua) ? 'Safari' : 'navegador';
    return `${so} · ${nav}`;
  }

  // Troca o código do QR ou o PIN pelo token DESTE aparelho. É o token que vai
  // nos pedidos; o código e o PIN só servem para este passo.
  async function parear(credencial) {
    let r;
    try {
      r = await fetch('/api/parear', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ...credencial, nome: nomeDoAparelho() }),
        cache: 'no-store',
      });
    } catch {
      throw new Error('O computador não respondeu. Ele está ligado, com o servidor no ar e a VPN conectada?');
    }
    const corpo = await r.json().catch(() => ({}));
    if (!r.ok || !corpo.token) throw new Error(corpo.erro || `Erro ${r.status}`);
    guardarToken(corpo.token);
  }

  function pedirPareamento(msg) {
    moldura(false);
    estado('Não pareado');
    const erro = h('p', { class: 'txt-perigo', role: 'alert', hidden: !msg }, msg || '');
    const pin = h('input', {
      class: 'campo pin', type: 'password', inputmode: 'numeric', pattern: '[0-9]*',
      autocomplete: 'one-time-code', maxlength: '12', placeholder: 'PIN', 'aria-label': 'PIN',
    });
    const link = h('input', {
      class: 'campo', type: 'text', inputmode: 'url', autocomplete: 'off', autocapitalize: 'off',
      spellcheck: 'false', placeholder: 'Cole o link copiado', 'aria-label': 'Link de pareamento',
    });
    const botao = h('button', { class: 'botao primario', type: 'button' }, 'Parear');

    const entrar = async () => {
      const vPin = pin.value.trim();
      const m = /#t=([A-Za-z0-9_-]+)/.exec(link.value.trim());
      let credencial = null;
      if (m) credencial = { codigo: m[1] };
      else if (/^[0-9]{6,12}$/.test(vPin)) credencial = { pin: vPin };
      if (!credencial) {
        erro.textContent = vPin ? 'O PIN tem de 6 a 12 números.' : 'Digite o PIN ou cole o link.';
        erro.hidden = false;
        pin.focus();
        return;
      }
      botao.disabled = true;
      botao.textContent = 'Pareando…';
      try {
        await parear(credencial);
        iniciar();
      } catch (e) {
        erro.textContent = e.message;
        erro.hidden = false;
        pin.value = '';
        botao.disabled = false;
        botao.textContent = 'Parear';
      }
    };
    botao.addEventListener('click', entrar);
    for (const c of [pin, link]) c.addEventListener('keydown', (e) => { if (e.key === 'Enter') entrar(); });

    mostrar(h('div', { class: 'centro' }, h('div', { class: 'pareamento' },
      h('h1', null, 'Parear com o computador'),
      h('p', null, 'No computador, abra o SIGAA Viewer na aba Acesso mobile e ligue o servidor.'),
      erro,
      h('span', { class: 'rotulo' }, 'Com o PIN'),
      h('p', { class: 'pequeno' }, 'O PIN que você criou na aba Acesso mobile.'),
      pin,
      botao,
      h('details', { class: 'alternativa' },
        h('summary', null, 'Sem PIN? Use o QR code ou o link'),
        h('p', { class: 'pequeno' }, 'Aponte a câmera para o QR code da aba, ou copie o link lá e cole aqui.'),
        link))));
    pin.focus();
  }

  // O link do QR traz o código depois do # (que o navegador nunca envia). Ele
  // sai da barra de endereço na hora, para não ficar no histórico nem num
  // print de tela, e é trocado pelo token deste aparelho.
  async function lerCodigoDoLink() {
    const m = /^#t=([A-Za-z0-9_-]+)/.exec(location.hash);
    if (!m) return;
    history.replaceState(null, '', `${location.pathname}#/agenda`);
    try {
      await parear({ codigo: m[1] });
    } catch (e) {
      pedirPareamento(e.message);
      throw e;
    }
  }

  // --- telas --------------------------------------------------------------------
  let semanaAgenda = null;   // ISO da segunda-feira em vista; null = esta semana

  // O texto do SIGAA chega com a indentação do HTML de onde saiu: espaços em
  // fila e linhas em branco que, com white-space: pre-wrap, viram buracos.
  function limparTexto(t) {
    return t.replace(/[ \t\u00a0]+/g, ' ').replace(/ *\n[ \n]*/g, '\n').trim();
  }

  function avisoVazio(titulo, texto) {
    return h('div', { class: 'aviso-vazio' }, h('strong', null, titulo), h('p', null, texto));
  }

  function faltasTexto(f) {
    if (!f) return 'Faltas —';
    return `Faltas ${f.faltas}/${f.limite}`;
  }
  function classeFaltas(f) {
    if (!f) return null;
    if (f.reprovado || f.faltas >= f.limite) return 'txt-perigo';
    if (f.limite > 0 && f.faltas >= f.limite * 0.75) return 'txt-aviso';
    return null;
  }

  function cartaoPrazo(p) {
    return h('a', { class: 'cartao coluna', href: `#/turmas/${p.turma_id}` },
      h('span', { class: 'linha meta' }, etiquetaPrazo(p.dias_ate),
        h('span', null, `${diaCurto(lerData(p.prazo))}${hora(p.prazo) ? ` · ${hora(p.prazo)}` : ''}`)),
      h('span', { class: 'titulo-cartao' }, p.titulo),
      h('span', { class: 'sigla' }, `${p.turma}${p.tipo ? ` · ${p.tipo}` : ''}`));
  }

  async function telaAgenda() {
    marcarNav('agenda');
    const q = semanaAgenda ? `?semana=${semanaAgenda}` : '';
    const a = await api(`/api/agenda${q}`);
    const ini = lerData(a.semana.inicio);
    const fim = lerData(a.semana.fim);
    const desloc = a.semana.deslocamento;

    const irPara = (n) => {
      const d = new Date(ini);
      d.setDate(d.getDate() + n * 7);
      semanaAgenda = isoDia(d);
      render();
    };

    let nHoje = 0, nAmanha = 0, nSemana = 0;
    for (const d of a.dias) {
      nSemana += d.aulas.length;
      if (d.hoje) nHoje = d.aulas.length;
    }
    const amanha = new Date();
    amanha.setDate(amanha.getDate() + 1);
    const dAmanha = a.dias.find((d) => d.data === isoDia(amanha));
    if (dAmanha) nAmanha = dAmanha.aulas.length;

    const cab = h('div', { class: 'secao' },
      h('div', { class: 'linha' },
        h('button', { class: 'botao quadrado', type: 'button', 'aria-label': 'Semana anterior', onclick: () => irPara(-1) }, '◀'),
        h('div', { class: 'semana' },
          h('strong', null, `${diaMes(ini)} – ${diaMes(fim)}`),
          h('span', null, relSemana(desloc))),
        h('button', { class: 'botao quadrado', type: 'button', 'aria-label': 'Próxima semana', onclick: () => irPara(1) }, '▶')),
      h('div', { class: 'linha' },
        desloc === 0
          ? h('span', { class: 'segundo' }, h('strong', null, nHoje), ' hoje · ', h('strong', null, nAmanha), ' amanhã · ', h('strong', null, nSemana), ' na semana')
          : h('span', { class: 'segundo' }, h('strong', null, nSemana), ' aulas na semana'),
        h('button', {
          class: 'botao pequeno empurra', type: 'button', disabled: desloc === 0,
          onclick: () => { semanaAgenda = null; render(); },
        }, 'Hoje')));

    let dias;
    if (!a.coletado) {
      dias = avisoVazio('Ainda não coletei as aulas',
        'O computador ainda não buscou suas turmas no SIGAA. Abra o app lá e clique em Atualizar.');
    } else {
      // Na semana de hoje, os dias que já passaram ficam atrás de um botão:
      // no celular, quinta-feira abriria com três dias velhos antes do "Hoje".
      const iHoje = a.dias.findIndex((d) => d.hoje);
      const passados = iHoje > 0 ? a.dias.slice(0, iHoje).filter((d) => d.aulas.length).length : 0;
      let verPassados = false;
      const botaoPassados = passados ? h('button', {
        class: 'recolher', type: 'button', 'aria-expanded': 'false',
        onclick: () => {
          verPassados = !verPassados;
          for (const el of dias.querySelectorAll('[data-passado]')) el.hidden = !verPassados;
          botaoPassados.setAttribute('aria-expanded', String(verPassados));
          botaoPassados.firstChild.textContent = verPassados ? '▾' : '▸';
        },
      }, h('span', null, '▸'), `Dias anteriores desta semana (${passados})`) : null;
      dias = h('section', { class: 'secao' }, botaoPassados, a.dias.map((d, i) => {
        const passado = passados > 0 && i < iHoje;
        const el = diaAgenda(d, i);
        if (el && passado) { el.dataset.passado = ''; el.hidden = true; }
        return el;
      }));
    }

    function diaAgenda(d, i) {
      const data = lerData(d.data);
      // Fim de semana só aparece quando há aula: sábado vazio é ruído.
      if (i >= 5 && d.aulas.length === 0) return null;
      if (d.aulas.length === 0) {
        return h('div', { class: 'vazio-dia' }, `${diaCurto(data)} · não há aula neste dia`);
      }
      const dif = Math.round((data - new Date(new Date().toDateString())) / 86400000);
      const titulo = h('div', { class: 'dia-titulo' },
        d.hoje ? h('span', { class: 'hoje' }, 'Hoje — ') : (dif === 1 ? h('span', { class: 'rel' }, 'Amanhã — ') : null),
        diaCurto(data));
      return h('div', { class: 'secao' }, titulo, d.aulas.map((au) =>
        h('a', { class: 'cartao', href: `#/turmas/${au.turma_id}` },
          h('span', { class: 'horario mono' }, au.horario || '—'),
          h('span', { class: 'corpo' },
            h('span', { class: 'sigla' }, au.turma),
            h('span', { class: 'linha titulo-cartao' },
              au.topico ? h('span', null, au.topico) : h('span', { class: 'sutil' }, h('i', null, 'sem tópico registrado')),
              au.extra ? h('span', { class: 'pilula' }, 'extra') : null),
            h('span', { class: 'meta' },
              au.local ? h('span', null, au.local) : null,
              h('span', { class: classeFaltas(au.faltas) }, faltasTexto(au.faltas)))))));
    }

    const prazos = h('section', { class: 'secao' },
      h('h2', { class: 'rotulo' }, 'Prazos atuais'),
      a.prazos.length ? a.prazos.map(cartaoPrazo) : h('div', { class: 'vazio-dia' }, 'Nenhum prazo em aberto.'));

    mostrar(h('div', { class: 'pagina' }, cab, dias, prazos));
  }

  function etiquetasProva(p) {
    const e = [];
    if (p.estado === 'corrigida') e.push(h('span', { class: 'pilula ok' }, '✓ você corrigiu'));
    else if (p.estado === 'deduzida') e.push(h('span', { class: 'pilula aviso' }, '? deduzida — confirme'));
    else if (p.estado === 'criada') e.push(h('span', { class: 'pilula ok' }, '✓ você criou'));
    else if (p.estado === 'confirmada') e.push(h('span', { class: 'segundo pequeno' }, 'você confirmou'));
    else if (p.estado === 'conflito') e.push(h('span', { class: 'pilula perigo' }, '! o SIGAA mudou a data'));
    else e.push(h('span', { class: 'segundo pequeno' }, 'painel do professor'));
    if (p.data_sigaa) e.push(h('span', { class: 'sutil pequeno' }, `SIGAA diz ${diaMes(lerData(p.data_sigaa))}`));
    return e;
  }

  function pontoProva(p) {
    if (p.dias_ate < 0) return 'ponto passado';
    if (p.estado === 'deduzida') return 'ponto vazado';
    return p.dias_ate <= 7 ? 'ponto urgente' : 'ponto';
  }

  function cartaoProva(p) {
    const d = lerData(p.data);
    return h('a', { class: 'cartao', href: `#/turmas/${p.turma_id}`, id: `prova-${p.turma_id}-${p.data}` },
      h('span', { class: 'data-bloco' },
        h('span', { class: 'ds' }, DS[d.getDay()]),
        h('span', { class: 'dd' }, dd(d.getDate())),
        h('span', { class: 'mm' }, MES[d.getMonth()]),
        h('span', { class: pontoProva(p), 'aria-hidden': 'true' })),
      h('span', { class: 'corpo' },
        h('span', { class: 'titulo-cartao forte' }, p.descricao, ' · ', h('span', { class: 'pequeno' }, p.turma)),
        h('span', { class: 'meta' }, `${hora(p.data) || 'sem horário'} · ${relativo(p.dias_ate)}${p.local ? ` · ${p.local}` : ''}`),
        h('span', { class: 'linha' }, etiquetasProva(p)),
        p.nota ? h('span', { class: 'sutil pequeno' }, `“${p.nota}”`) : null));
  }

  async function telaProvas() {
    marcarNav('provas');
    const [dp, da] = await Promise.all([api('/api/provas'), api('/api/agenda')]);
    const futuras = dp.provas.filter((p) => p.dias_ate >= 0);
    const passadas = dp.provas.filter((p) => p.dias_ate < 0).reverse();
    const partes = [h('h1', null, 'Provas')];

    if (!dp.provas.length) {
      partes.push(avisoVazio('Nenhuma prova conhecida',
        'As provas vêm de dentro das turmas. No computador, use “Atualizar tudo” para o app entrar nelas.'));
      mostrar(h('div', { class: 'pagina' }, partes));
      return;
    }

    const prox = futuras[0];
    if (prox) {
      const d = lerData(prox.data);
      const urg = prox.dias_ate <= 7;
      partes.push(h('a', { class: 'cartao grande', href: `#/turmas/${prox.turma_id}` },
        h('span', { class: 'linha-base' },
          h('span', { class: 'rotulo' }, 'Próxima prova'),
          h('span', { class: urg ? 'txt-aviso pequeno' : 'segundo pequeno' }, `◷ ${relativo(prox.dias_ate)}`)),
        h('strong', null, `${prox.descricao} · ${prox.turma}`),
        h('span', { class: 'segundo pequeno' },
          [diaCurto(d), hora(prox.data), prox.local].filter(Boolean).join(' · ')),
        h('span', { class: 'linha' }, etiquetasProva(prox))));
    }

    const aConfirmar = futuras.filter((p) => p.estado === 'deduzida');
    if (aConfirmar.length) {
      partes.push(h('div', { class: 'cartao grande' },
        h('span', { class: 'linha-base' }, h('span', { class: 'rotulo' }, 'A confirmar'),
          h('span', { class: 'txt-aviso pequeno' }, `○ ${aConfirmar.length}`)),
        aConfirmar.slice(0, 3).map((p) => h('strong', { class: 'pequeno' },
          `${p.descricao} · ${p.turma} · ${diaCurto(lerData(p.data))}`)),
        aConfirmar.length > 3 ? h('span', { class: 'segundo pequeno' }, `e mais ${aConfirmar.length - 3}, marcadas com ○ na lista abaixo.`) : null,
        h('span', { class: 'segundo pequeno' },
          'Datas deduzidas do título de um tópico de aula. Confirme ou corrija no computador.')));
    }

    // Carga das próximas seis semanas: provas cheias, entregas vazadas.
    const seg0 = segunda(new Date());
    const semanas = Array.from({ length: 6 }, (_, i) => {
      const s = new Date(seg0);
      s.setDate(s.getDate() + i * 7);
      return { ini: s, provas: 0, entregas: 0 };
    });
    const daSemana = (iso) => {
      const n = Math.floor((segunda(lerData(iso)) - seg0) / (7 * 86400000));
      return n >= 0 && n < 6 ? semanas[n] : null;
    };
    for (const p of futuras) { const s = daSemana(p.data); if (s) s.provas++; }
    for (const p of da.prazos) { if (p.dias_ate >= 0) { const s = daSemana(p.prazo); if (s) s.entregas++; } }
    partes.push(h('div', { class: 'secao' },
      h('span', { class: 'rotulo' }, 'Carga por semana'),
      h('div', { class: 'carga' }, semanas.map((s, i) => {
        const blocos = [];
        for (let k = 0; k < Math.min(s.provas, 5); k++) blocos.push(h('span', { class: 'bloco' }));
        for (let k = 0; k < Math.min(s.entregas, 5 - Math.min(s.provas, 5)); k++) blocos.push(h('span', { class: 'bloco entrega' }));
        return h('div', {
          class: i === 0 ? 'col atual' : 'col',
          'aria-label': `Semana de ${diaMes(s.ini)}: ${s.provas} prova(s), ${s.entregas} entrega(s)`,
        }, h('span', { class: 'pilha' }, blocos), h('span', { class: 'rot' }, diaMes(s.ini)));
      })),
      h('span', { class: 'legenda' }, h('span', null, h('span', { class: 'txt-aviso' }, '■'), ' prova'), h('span', null, '□ entrega'))));

    partes.push(h('div', { class: 'legenda' },
      h('span', null, '● data cadastrada'), h('span', null, '○ deduzida — confirme'),
      h('span', { class: 'txt-aviso' }, '● próximos 7 dias')));

    // Agrupadas por semana, como o calendário do computador.
    const grupos = new Map();
    for (const p of futuras) {
      const k = isoDia(segunda(lerData(p.data)));
      if (!grupos.has(k)) grupos.set(k, []);
      grupos.get(k).push(p);
    }
    for (const [k, lista] of grupos) {
      const ini = lerData(k);
      const n = Math.round((ini - seg0) / (7 * 86400000));
      partes.push(h('section', { class: 'secao' },
        h('div', { class: 'linha-base' }, h('strong', { class: 'pequeno' }, `Semana de ${diaMes(ini)}`),
          h('span', { class: 'sutil pequeno' }, relSemana(n))),
        lista.map(cartaoProva)));
    }

    if (passadas.length) {
      const caixa = h('div', { class: 'passadas', hidden: true }, passadas.map((p) =>
        h('div', null, `${diaCurto(lerData(p.data))} · ${p.descricao} · ${p.turma}`)));
      const botao = h('button', {
        class: 'recolher', type: 'button', 'aria-expanded': 'false',
        onclick: () => {
          caixa.hidden = !caixa.hidden;
          botao.setAttribute('aria-expanded', String(!caixa.hidden));
          botao.firstChild.textContent = caixa.hidden ? '▸' : '▾';
        },
      }, h('span', null, '▸'), `Provas antigas (${passadas.length})`);
      partes.push(botao, caixa);
    }
    mostrar(h('div', { class: 'pagina' }, partes));
  }

  async function telaTurmas() {
    marcarNav('turmas');
    const d = await api('/api/turmas');
    const partes = [h('div', { class: 'linha-base' }, h('h1', null, 'Turmas'),
      resumo && resumo.periodo ? h('span', { class: 'sutil' }, resumo.periodo) : null)];
    if (!d.turmas.length) {
      partes.push(avisoVazio('Nenhuma turma ainda', 'Abra o app no computador e clique em Atualizar.'));
    }
    for (const t of d.turmas) {
      const pp = t.proxima_prova;
      partes.push(h('a', { class: 'cartao', href: `#/turmas/${t.id}` },
        h('span', { class: 'corpo' },
          h('span', { class: 'titulo-cartao forte' }, t.nome),
          h('span', { class: 'sigla mono' }, [t.codigo, t.horario].filter(Boolean).join(' · ')),
          h('span', { class: 'meta' },
            h('span', { class: classeFaltas(t.faltas) }, faltasTexto(t.faltas)),
            pp ? h('span', { class: pp.dias_ate <= 7 ? 'txt-aviso' : null }, `${pp.descricao} ${relativo(pp.dias_ate)}`) : null,
            t.arquivos ? h('span', null, `${t.arquivos} arquivo(s) · ${t.baixados} no computador`) : null)),
        h('span', { class: 'sutil', 'aria-hidden': 'true' }, '›')));
    }
    mostrar(h('div', { class: 'pagina' }, partes));
  }

  const ABAS_TURMA = [
    ['aulas', 'Aulas'], ['arquivos', 'Arquivos'], ['noticias', 'Notícias'],
    ['presenca', 'Presença'], ['provas', 'Provas'],
  ];

  async function telaTurma(id, aba) {
    marcarNav('turmas');
    const d = await api(`/api/turmas/${encodeURIComponent(id)}`);
    const t = d.turma;
    if (!ABAS_TURMA.some(([k]) => k === aba)) aba = 'aulas';

    const conteudo = [];
    if (aba === 'aulas') {
      if (!d.topicos.length) conteudo.push(avisoVazio('Sem tópicos de aula', 'O app ainda não entrou nesta turma, ou o professor não registrou nada.'));
      else {
        conteudo.push(h('div', { class: 'lista' }, d.topicos.map((tp) => {
          const ini = lerData(tp.inicio), fim = lerData(tp.fim);
          const faixa = ini ? (fim && isoDia(fim) !== isoDia(ini) ? `${diaMes(ini)}–${diaMes(fim)}` : diaMes(ini)) : null;
          // O tópico em andamento já vem aberto: é o que o aluno procura.
          const hojeIso = isoDia(new Date());
          const emCurso = ini && isoDia(ini) <= hojeIso && isoDia(fim || ini) >= hojeIso;
          return h('details', { class: 'topico', open: emCurso },
            h('summary', null, h('span', null, tp.titulo), faixa ? h('span', { class: 'mono sutil pequeno empurra faixa' }, faixa) : null),
            tp.conteudo ? h('div', { class: 'conteudo' }, limparTexto(tp.conteudo)) : null,
            tp.materiais.length
              ? h('div', { class: 'conteudo' }, tp.materiais.map((m) => h('div', null, `• ${m.titulo}${m.tipo ? ` (${m.tipo})` : ''}`)))
              : null);
        })));
      }
    } else if (aba === 'arquivos') {
      if (!d.arquivos.length) conteudo.push(avisoVazio('Nenhum arquivo', 'O professor não publicou arquivos, ou o app ainda não leu esta aba.'));
      else {
        conteudo.push(h('div', { class: 'lista' }, d.arquivos.map((a) => {
          const botao = a.baixado ? h('button', { class: 'botao pequeno', type: 'button' }, 'Abrir') : null;
          if (botao) botao.addEventListener('click', () => baixar(t.id, a, botao));
          return h('div', { class: 'item' },
            h('span', { class: 'corpo' },
              h('span', { class: 'titulo-cartao' }, a.titulo),
              a.topico ? h('span', { class: 'sutil pequeno' }, a.topico) : null),
            botao || h('span', { class: 'sutil pequeno' }, 'não baixado'));
        })));
        conteudo.push(h('p', { class: 'sutil pequeno' },
          '“Abrir” traz a cópia que o computador já baixou. O que não foi baixado lá não sai do SIGAA por aqui.'));
      }
    } else if (aba === 'noticias') {
      if (!d.noticias.length) conteudo.push(avisoVazio('Nenhuma notícia', 'Nada publicado pelo professor nesta turma, até a última coleta.'));
      else {
        conteudo.push(h('div', { class: 'lista' }, d.noticias.map((n) => h('div', { class: 'noticia' },
          h('strong', null, n.titulo),
          h('span', { class: 'sutil pequeno' }, [n.data ? diaCurto(lerData(n.data)) : null, n.autor].filter(Boolean).join(' · ')),
          n.texto ? h('div', { class: 'texto' }, limparTexto(n.texto)) : null))));
      }
    } else if (aba === 'presenca') {
      const f = d.frequencia;
      if (!f) conteudo.push(avisoVazio('Sem frequência lançada', 'O professor ainda não registrou presença nesta turma — o que é diferente de zero faltas.'));
      else {
        conteudo.push(h('p', { class: 'texto-longo' },
          h('strong', { class: classeFaltas(f) }, `${f.faltas} falta(s) de ${f.limite} permitidas`),
          `, segundo o diário do professor (${f.presencas} presenças em ${f.aulas_registradas} aulas registradas).`));
        conteudo.push(h('div', { class: 'lista' }, [...f.dias].reverse().map((dia) => {
          let sit;
          if (dia.situacao === 'falta') sit = h('span', { class: 'pilula perigo' }, `✕ ${dia.faltas} falta(s)`);
          else if (dia.situacao === 'presente') sit = h('span', { class: 'segundo' }, 'Presente');
          else sit = h('span', { class: 'sutil' }, 'não registrado');
          return h('div', { class: 'item' },
            h('span', { class: 'mono pequeno' }, dia.data ? diaCurto(lerData(dia.data)) : '—'),
            h('span', { class: 'corpo' }, sit),
            dia.estado === 'marcada' ? h('span', { class: 'txt-ok pequeno' }, '✎ você') : null,
            dia.estado === 'conflito' ? h('span', { class: 'txt-perigo pequeno' }, 'conflito') : null);
        })));
      }
    } else if (aba === 'provas') {
      if (!d.provas.length) conteudo.push(avisoVazio('Nenhuma prova', 'Nada cadastrado nem deduzido para esta turma.'));
      else conteudo.push(h('div', { class: 'secao' }, d.provas.map(cartaoProva)));
    }

    mostrar(h('div', { class: 'pagina' },
      h('a', { class: 'voltar', href: '#/turmas' }, '‹ Turmas'),
      h('div', { class: 'secao' },
        h('h1', null, t.nome),
        h('span', { class: 'segundo pequeno' }, [t.codigo, t.horario, t.local].filter(Boolean).join(' · '))),
      h('nav', { class: 'abas', role: 'tablist', 'aria-label': 'Seções da turma' }, ABAS_TURMA.map(([k, rot]) =>
        h('a', { role: 'tab', href: `#/turmas/${t.id}/${k}`, 'aria-selected': String(k === aba) }, rot))),
      conteudo));
  }

  async function telaNovidades() {
    marcarNav('novidades');
    const d = await api('/api/novidades');
    const partes = [h('h1', null, 'Novidades')];
    if (!d.novidades.length) {
      partes.push(avisoVazio('Nada novo', 'O que o portal do SIGAA anunciar — tarefa nova, arquivo novo — aparece aqui.'));
    }
    const hoje = new Date(new Date().toDateString());
    let grupoAtual = null, lista = null;
    for (const n of d.novidades) {
      const k = n.data || 'sem data';
      if (k !== grupoAtual) {
        grupoAtual = k;
        const dt = lerData(n.data);
        const dif = dt ? Math.round((dt - hoje) / 86400000) : null;
        const rot = dt ? (dif >= -1 ? `${relativo(dif)} · ${diaCurto(dt)}` : diaCurto(dt)) : 'sem data';
        lista = h('div', { class: 'lista' });
        partes.push(h('section', { class: 'secao' }, h('h2', { class: 'rotulo' }, rot), lista));
      }
      lista.append(h('a', { class: 'item cartao', href: `#/turmas/${n.turma_id}` },
        h('span', { class: 'corpo' }, h('span', { class: 'sigla' }, n.turma), h('span', { class: 'titulo-cartao' }, n.texto))));
    }
    mostrar(h('div', { class: 'pagina' }, partes));
  }

  // --- roteamento ------------------------------------------------------------
  let geracao = 0;

  async function render() {
    const minha = ++geracao;
    const [, secao, id, aba] = (location.hash.replace(/^#/, '') || '/agenda').split('/');
    try {
      if (!resumo) await carregarResumo();
      moldura(true);
      if (secao === 'provas') await telaProvas();
      else if (secao === 'turmas' && id) await telaTurma(id, aba);
      else if (secao === 'turmas') await telaTurmas();
      else if (secao === 'novidades') await telaNovidades();
      else await telaAgenda();
    } catch (e) {
      if (minha !== geracao) return;   // o aluno já trocou de tela
      if (e instanceof SemPareamento) {
        const tinha = !!token();
        tokenEmMemoria = null;
        guardado.apagar(CHAVE_TOKEN);
        pedirPareamento(tinha ? 'Este celular foi desconectado no computador. Pareie de novo.' : null);
        return;
      }
      moldura(true);
      estado('Sem conexão com o computador');
      mostrar(h('div', { class: 'pagina' }, h('div', { class: 'aviso-vazio erro' },
        h('strong', null, 'Não consegui carregar'),
        h('p', null, e.message),
        h('button', { class: 'botao', type: 'button', onclick: () => { resumo = null; render(); } }, 'Tentar de novo'))));
    }
  }

  function iniciar() {
    resumo = null;
    if (!token()) { pedirPareamento(); return; }
    render();
  }

  document.addEventListener('DOMContentLoaded', () => {
    aplicarTema(guardado.ler(CHAVE_TEMA) || 'sistema');
    $('mais').addEventListener('click', abrirMenu);
    $('recarregar').addEventListener('click', () => { resumo = null; render(); });
    window.addEventListener('hashchange', () => {
      if (/^#t=/.test(location.hash)) { lerCodigoDoLink().then(iniciar, () => {}); return; }
      render();
    });
    // Voltar ao app depois de um tempo: o computador pode ter sincronizado.
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible' && token()) { resumo = null; render(); }
    });
    lerCodigoDoLink().then(iniciar, () => {});
  });
})();
