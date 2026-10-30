/**
 * @file hardware_specs.h
 * @brief Definições de hardware e especificações físicas do chip Antikythera-8 OS.
 * @note ESTE ARQUIVO NÃO DEVE SER MODIFICADO PELOS ALUNOS.
 */

#ifndef HARDWARE_SPECS_H
#define HARDWARE_SPECS_H

#include <stdbool.h>
#include <stdint.h>

/* --- LIMITES E CONSTANTES DO HARDWARE ANTIDYTHERA-8 --- */
#define VIRTUAL_MEMORY_SIZE (1 * 1024 * 1024) /* 1 MB de Espaço Virtual */
#define PHYSICAL_RAM_SIZE   (256 * 1024)       /* 256 KB de RAM Física */

#define PAGE_SIZE_EVEN      4096  /* Páginas com ID Par: 4 KB */
#define PAGE_SIZE_ODD       8192  /* Páginas com ID Ímpar: 8 KB */

#define FADING_THRESHOLD    5     /* Bit R sofre fading a cada 5 acessos globais */
#define BASE_SWAP_TEMP      25.0f /* Temperatura ambiente/inicial do Swap em °C */
#define MAX_SAFE_TEMP       65.0f /* Limite antes do Thermal Throttling */

/* --- ESTRUTURAS DE DADOS --- */

/**
 * @brief Entrada da Tabela de Páginas (PTE) do Antikythera-8.
 */
typedef struct {
    uint32_t frame_number;  /* Número do frame na RAM física (se presente) */
    bool present;           /* Bit P: 1 se está na RAM, 0 se está no Swap */
    bool referenced;        /* Bit R: Referenciada recentemente */
    bool modified;          /* Bit M: Suja (modificada), precisa de Page Out */
    uint32_t last_access_t; /* Timestamp do último acesso (usado pelo WSClock) */
} page_table_entry_t;

/**
 * @brief Estrutura que representa o estado atual dos registradores de métricas de Hardware.
 */
typedef struct {
    uint64_t global_access_count; /* Contador global de acessos à MMU */
    float swap_temperature;       /* Temperatura atual do dispositivo de Swap (°C) */
    uint64_t total_cpu_cycles;    /* Ciclos de CPU acumulados (incluindo penalidades) */
    uint32_t last_swap_ms;        /* Controle interno de tempo para pico térmico */
} hardware_state_t;

/* --- COMPONENTES DO MOTOR EM C (MMU & HARDWARE) --- */

/**
 * @brief Inicializa o estado físico do hardware, limpando estatísticas e aquecimento.
 * @param state Ponteiro para a estrutura de estado do hardware.
 */
void hardware_init(hardware_state_t *state);

/**
 * @brief Traduz e valida o acesso à memória via MMU.
 * Simula a paginação assimétrica e injeta a anomalia elétrica do Fading do Bit R.
 * 
 * @param state Estado atual do hardware.
 * @param page_table Vetor que representa a tabela de páginas do processo.
 * @param num_entries Quantidade total de entradas na tabela de páginas.
 * @param virtual_address Endereço virtual requisitado (0x00000 até 0xFFFFF).
 * @param is_write true se for operação de Escrita (ativa bit M), false para Leitura.
 * @param current_timestamp Timestamp virtual do acesso.
 * @return true em caso de Hit (Página na RAM). false em caso de Page Fault.
 */
bool mmu_access_memory(hardware_state_t *state, 
                        page_table_entry_t *page_table, 
                        uint32_t num_entries,
                        uint32_t virtual_address, 
                        bool is_write,
                        uint32_t current_timestamp);

/**
 * @brief Simula o impacto físico e térmico de uma operação de escrita no Swap (Page Out).
 * Deve ser chamado pelo SO toda vez que uma página suja (M=1) for despejada.
 * 
 * @param state Estado atual do hardware.
 * @param current_timestamp_ms O timestamp atual convertido ou tratado em milissegundos.
 */
void hardware_trigger_page_out(hardware_state_t *state, uint32_t current_timestamp_ms);

/**
 * @brief Aplica o resfriamento natural do Swap quando o tempo passa sem operações de I/O.
 * @param state Estado atual do hardware.
 * @param elapsed_ms Milissegundos que se passaram desde o último evento.
 */
void hardware_cool_down(hardware_state_t *state, uint32_t elapsed_ms);

#endif /* HARDWARE_SPECS_H */
