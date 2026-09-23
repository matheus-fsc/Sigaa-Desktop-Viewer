#pragma once
// SHA-256 (FIPS 180-4), em ~80 linhas e sem dependência.
//
// POR QUE IMPLEMENTAR EM VEZ DE LINKAR OPENSSL: o app já usa libcurl, que no
// Linux costuma trazer OpenSSL junto — mas no Windows o vcpkg compila a curl
// contra a Schannel, e ali não há OpenSSL nenhum. Contar com o que veio de
// carona quebraria numa plataforma só, e só no dia do primeiro download.
//
// Escrever criptografia é normalmente má ideia. Este caso é a exceção estreita
// em que não é: SHA-256 é uma função de hash sem chave e sem estado secreto,
// o algoritmo está publicado até a última constante, e a implementação é
// conferível contra os vetores do NIST — que é o que tests/sha256_test.cpp
// faz. Não há canal lateral a proteger nem segredo a vazar; ou o digest bate
// com o de referência, ou não bate.

#include <cstddef>
#include <cstdint>
#include <string>

namespace sigaa::hash {

// O digest em hexadecimal minúsculo, 64 caracteres.
std::string sha256Hex(const void* dados, std::size_t tamanho);
std::string sha256Hex(const std::string& s);

// Para arquivo grande, em blocos — o AppImage tem ~90 MB e não cabe na
// memória sem motivo.
class Sha256 {
public:
    Sha256();
    void consumir(const void* dados, std::size_t tamanho);
    std::string hex();   // finaliza; não use `consumir` depois

private:
    void bloco(const std::uint8_t* p);

    std::uint32_t estado_[8];
    std::uint8_t resto_[64];
    std::size_t restoTam_{0};
    std::uint64_t bits_{0};
};

} // namespace sigaa::hash
