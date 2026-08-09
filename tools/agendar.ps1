<#
.SYNOPSIS
    Instala, remove ou inspeciona as tarefas agendadas do sigaa-viewer.

.NOTES
    OPCIONAL, E NAO INSTALADO POR PADRAO.

    O comportamento normal do app e sincronizar apenas com a janela aberta
    (botao "Automatico" na interface). Nada roda com o programa fechado.

    Este script existe para quem quiser aviso de prazo mesmo sem abrir o app.
    EFEITO COLATERAL CONHECIDO: sigaa-cli.exe e um binario de CONSOLE, entao
    cada execucao pisca uma janela preta na tela. O -Hidden abaixo esconde a
    tarefa na LISTA do Agendador, nao a janela; quando esconderConsole() roda
    dentro do processo, o Windows ja mostrou o console. A correcao de verdade e
    um segundo binario ligado com /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup, que
    nao cria console nenhum — nao foi feito porque a decisao foi nao rodar em
    segundo plano.

.DESCRIPTION
    Cria DUAS tarefas, de proposito, com frequencias diferentes:

      SIGAA - prazos    a cada 20 min   1 requisicao   portal (atividades, avisos)
      SIGAA - turmas    3x ao dia       ~15 requisicoes turma virtual (provas, topicos)

    A separacao nao e frescura. Prazo de atividade muda a qualquer hora e vem do
    portal por UMA requisicao; entrar nas 7 turmas custa ~15 requisicoes e leva
    meio minuto, e prova quase nunca muda. Rodar `--turmas` a cada 20 minutos
    seriam ~500 visitas por dia a um servidor de universidade para descobrir
    quase sempre a mesma coisa. Ver as regras de etiqueta em docs/PLANO.md.

    Isso e seguro porque o banco faz upsert: um sync so-portal nao apaga as
    provas ja conhecidas, e o DiffEngine ignora avaliacoes quando a coleta veio
    sem turmas (tests/diff_engine_test.cpp, "sync sem --turmas nao alarma").

.PARAMETER Instalar
    Registra (ou substitui) as duas tarefas.

.PARAMETER Remover
    Apaga as duas tarefas.

.PARAMETER Status
    Mostra estado, ultimo resultado e proxima execucao.

.PARAMETER Agora
    Dispara a tarefa completa uma vez, para testar.

.EXAMPLE
    .\tools\agendar.ps1 -Instalar
    .\tools\agendar.ps1 -Status
    .\tools\agendar.ps1 -Remover
#>
[CmdletBinding(DefaultParameterSetName = 'Status')]
param(
    [Parameter(ParameterSetName = 'Instalar')][switch]$Instalar,
    [Parameter(ParameterSetName = 'Remover')][switch]$Remover,
    [Parameter(ParameterSetName = 'Status')][switch]$Status,
    [Parameter(ParameterSetName = 'Agora')][switch]$Agora,

    [Parameter(ParameterSetName = 'Instalar')][int]$IntervaloMinutos = 20,
    [Parameter(ParameterSetName = 'Instalar')][string]$Exe
)

$ErrorActionPreference = 'Stop'

$TarefaPrazos = 'SIGAA - prazos'
$TarefaTurmas = 'SIGAA - turmas'
$Raiz = Split-Path -Parent $PSScriptRoot

function Resolver-Exe {
    param([string]$Informado)
    if ($Informado) {
        if (-not (Test-Path $Informado)) { throw "executavel nao encontrado: $Informado" }
        return (Resolve-Path $Informado).Path
    }
    $candidatos = @(
        "$Raiz\build\windows\Release\sigaa-cli.exe",
        "$Raiz\build\windows\Debug\sigaa-cli.exe"
    )
    foreach ($c in $candidatos) { if (Test-Path $c) { return (Resolve-Path $c).Path } }
    throw "sigaa-cli.exe nao encontrado. Compile antes, ou passe -Exe <caminho>."
}

function Checar-Credenciais {
    # --quiet NAO pergunta senha: um prompt numa janela oculta travaria a tarefa
    # para sempre. Melhor descobrir isso agora do que por silencio daqui a uma
    # semana.
    if ($env:SIGAA_LOGIN -and $env:SIGAA_SENHA) { return }
    $envFile = Join-Path $Raiz '.env'
    if (Test-Path $envFile) {
        $txt = Get-Content $envFile -Raw
        if ($txt -match '(?m)^\s*SIGAA_LOGIN\s*=\s*\S' -and
            $txt -match '(?m)^\s*SIGAA_SENHA\s*=\s*\S') { return }
    }
    throw ("credenciais ausentes: em --quiet o sync nao pergunta nada. " +
           "Preencha SIGAA_LOGIN e SIGAA_SENHA no .env (veja .env.example).")
}

function Novo-Gatilho-Repetido {
    param([int]$Minutos)
    # Gatilho -Once que comeca agora e repete indefinidamente.
    #
    # NAO usar AtLogOn aqui: registrar tarefa com gatilho de logon exige
    # elevacao, e o objetivo e o usuario conseguir instalar sem admin. O logon
    # fica coberto por -StartWhenAvailable: ao ligar o PC, a repeticao perdida
    # roda em vez de esperar o proximo intervalo.
    return New-ScheduledTaskTrigger -Once -At (Get-Date) `
        -RepetitionInterval (New-TimeSpan -Minutes $Minutos) `
        -RepetitionDuration (New-TimeSpan -Days 3650)
}

function Registrar {
    # NAO renomear $Argumentos para $Args: $Args e variavel automatica do
    # PowerShell e o parametro chega sempre vazio, sem erro no ponto da causa.
    param(
        [string]$Nome, [string]$ExePath, [string]$Argumentos,
        [object[]]$Gatilhos, [string]$Descricao
    )

    $acao = New-ScheduledTaskAction -Execute $ExePath -Argument $Argumentos -WorkingDirectory $Raiz

    # LogonType Interactive e obrigatorio: uma tarefa "rode esteja o usuario
    # logado ou nao" corre na sessao 0, onde NENHUMA notificacao aparece — e o
    # app perderia justamente a razao de existir. Alem disso, guardar a senha
    # do Windows no agendador seria pior que o .env.
    $principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" `
        -LogonType Interactive -RunLevel Limited

    $config = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -StartWhenAvailable `
        -RunOnlyIfNetworkAvailable `
        -ExecutionTimeLimit (New-TimeSpan -Minutes 10) `
        -MultipleInstances IgnoreNew `
        -Hidden

    # StartWhenAvailable cobre o notebook fechado: ao voltar, roda a execucao
    # perdida em vez de esperar mais 20 minutos.
    # IgnoreNew evita duas coletas concorrentes — o SIGAA invalida ViewState
    # quando ve navegacao paralela (docs/RECON.md).

    # Register-ScheduledTask erra de forma NAO terminante: sem esta verificacao
    # o script imprime "ok" para uma tarefa que nunca foi criada, e o usuario so
    # descobre quando o prazo passa. Confirmar no agendador e a unica prova.
    try {
        Register-ScheduledTask -TaskName $Nome -Action $acao -Trigger $Gatilhos `
            -Principal $principal -Settings $config -Description $Descricao `
            -Force -ErrorAction Stop | Out-Null
    } catch {
        Write-Host "  FALHOU  $Nome : $($_.Exception.Message)"
        if ($_.Exception.Message -match 'negado|denied') {
            Write-Host "          (rode o PowerShell como administrador)"
        }
        return $false
    }

    if (-not (Get-ScheduledTask -TaskName $Nome -ErrorAction SilentlyContinue)) {
        Write-Host "  FALHOU  $Nome : registrou sem erro mas nao aparece no agendador"
        return $false
    }

    Write-Host "  ok      $Nome"
    return $true
}

switch ($PSCmdlet.ParameterSetName) {

    'Instalar' {
        $exePath = Resolver-Exe -Informado $Exe
        Checar-Credenciais

        Write-Host "sigaa-viewer: registrando tarefas"
        Write-Host "  exe : $exePath"
        Write-Host "  cwd : $Raiz"

        $ok = $true
        $ok = (Registrar -Nome $TarefaPrazos -ExePath $exePath `
            -Argumentos "sync --quiet --log `"$Raiz\sigaa-viewer.log`" `"$Raiz\relatorio.html`"" `
            -Gatilhos @(Novo-Gatilho-Repetido -Minutos $IntervaloMinutos) `
            -Descricao "Verifica atividades e avisos no portal do SIGAA a cada $IntervaloMinutos min.") -and $ok

        # Manha, meio-dia e noite: pega o professor que marcou prova durante o
        # dia sem transformar isso em varredura de hora em hora.
        $gatilhosTurmas = @(
            (New-ScheduledTaskTrigger -Daily -At '07:30'),
            (New-ScheduledTaskTrigger -Daily -At '13:00'),
            (New-ScheduledTaskTrigger -Daily -At '20:00')
        )
        $ok = (Registrar -Nome $TarefaTurmas -ExePath $exePath `
            -Argumentos "sync --turmas --quiet --log `"$Raiz\sigaa-viewer.log`" `"$Raiz\relatorio.html`"" `
            -Gatilhos $gatilhosTurmas `
            -Descricao "Entra em cada turma virtual do SIGAA (provas e topicos), 3x ao dia.") -and $ok

        Write-Host ""
        if (-not $ok) {
            Write-Host "Instalacao INCOMPLETA — veja as linhas FALHOU acima."
            exit 1
        }
        Write-Host "Pronto. Teste agora com:  .\tools\agendar.ps1 -Agora"
        Write-Host "Log em: $Raiz\sigaa-viewer.log"
        Write-Host "Para desfazer:            .\tools\agendar.ps1 -Remover"
    }

    'Remover' {
        foreach ($n in @($TarefaPrazos, $TarefaTurmas)) {
            if (Get-ScheduledTask -TaskName $n -ErrorAction SilentlyContinue) {
                Unregister-ScheduledTask -TaskName $n -Confirm:$false
                Write-Host "  removida  $n"
            } else {
                Write-Host "  ausente   $n"
            }
        }
    }

    'Agora' {
        Start-ScheduledTask -TaskName $TarefaTurmas
        Write-Host "disparada: $TarefaTurmas (acompanhe em $Raiz\sigaa-viewer.log)"
    }

    default {
        foreach ($n in @($TarefaPrazos, $TarefaTurmas)) {
            $t = Get-ScheduledTask -TaskName $n -ErrorAction SilentlyContinue
            if (-not $t) { Write-Host "$n : nao instalada"; continue }
            $i = $t | Get-ScheduledTaskInfo
            # LastTaskResult 0 = ok. 267011 = nunca rodou ainda.
            Write-Host "$n"
            Write-Host "  estado        : $($t.State)"
            Write-Host "  ultima exec.  : $($i.LastRunTime)  (resultado $($i.LastTaskResult))"
            Write-Host "  proxima exec. : $($i.NextRunTime)"
        }
    }
}
