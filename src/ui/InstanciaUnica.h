#pragma once
// Uma instância do app por usuário — e a segunda traz a primeira para a frente.
//
// POR QUE ISTO IMPORTA AQUI, e não é só arrumação: duas instâncias do
// sigaa-ui são duas sessões abertas na MESMA conta do SIGAA, cada uma com o
// próprio login. Foi exatamente esse acúmulo que travou o login em 18/09/2026
// (docs/RECON.md §6.0) — com uma sessão viva, o POST de login da outra ficou
// sem resposta até estourar três timeouts de 45 s.
//
// Some-se o banco: as duas escrevem no mesmo SQLite e no mesmo manifesto de
// material baixado. O WAL do SQLite aguenta, mas as duas telas mostrariam
// estados diferentes do mesmo semestre, e o aluno acreditaria na que estivesse
// na frente.
//
// COMO FUNCIONA: a primeira instância abre um QLocalServer com um nome
// derivado do usuário. A segunda tenta CONECTAR nesse nome; conseguindo, sabe
// que já há um app rodando, manda um aviso e sai. A primeira recebe o aviso e
// levanta a janela.
//
// O NOME LEVA O USUÁRIO: em máquina compartilhada, dois alunos logados ao
// mesmo tempo (sessão trocada, terminal server) são duas pessoas com contas
// diferentes do SIGAA, e bloquear o segundo seria impedir o uso legítimo.

#include <QObject>
#include <QString>

class QLocalServer;

namespace sigaa::ui {

class InstanciaUnica : public QObject {
    Q_OBJECT
public:
    explicit InstanciaUnica(QObject* pai = nullptr);

    // Tenta assumir o posto de instância principal.
    //
    // `false` = já havia outra, que foi avisada para se mostrar; quem chamou
    // deve encerrar sem abrir janela nenhuma.
    bool assumir();

Q_SIGNALS:
    // Outra instância foi aberta e pediu para esta aparecer.
    void pediramParaMostrar();

private:
    QString nome_;
    QLocalServer* servidor_{nullptr};
};

} // namespace sigaa::ui
