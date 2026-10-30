/**
 * @file hardware_specs.c
 * @brief Implementação do comportamento físico e anomalias do Antikythera-8.
 */

#include "hardware_specs.h"
#include <stdio.h>
#include <stdlib.h>

void hardware_init(hardware_state_t *state) {
    if (state != NULL) {
        state->global_access_count = 0;
        state->swap_temperature = BASE_SWAP_TEMP;
        state->total_cpu_cycles = 0;
        state->last_swap_ms = 0;
    }
}

bool mmu_access_memory(hardware_state_t *state, 
                        page_table_entry_t *page_table, 
                        uint32_t num_entries,
                        uint32_t virtual_address, 
                        bool is_write,
                        uint32_t current_timestamp) {
    
    if (virtual_address >= VIRTUAL_MEMORY_SIZE) {
        /* Segmento fora dos limites do espaço virtual de 1MB */
        return false; 
    }

    state->global_access_count++;
    state->total_cpu_cycles += 1; /* Custo base de 1 ciclo por acesso à MMU */

    /* --- TRADUÇÃO DA PAGINAÇÃO ASSIMÉTRICA --- 
     * Descobrir a qual VPN pertence o endereço virtual de forma iterativa ou calculada,
     * já que os tamanhos alternam (Par = 4KB, Ímpar = 8KB).
     */
    uint32_t accumulated_address = 0;
    uint32_t vpn = 0;
    uint32_t current_page_size = 0;

    for (uint32_t i = 0; i < num_entries; i++) {
        current_page_size = (i % 2 == 0) ? PAGE_SIZE_EVEN : PAGE_SIZE_ODD;
        if (virtual_address >= accumulated_address && 
            virtual_address < accumulated_address + current_page_size) {
            vpn = i;
            break;
        }
        accumulated_address += current_page_size;
    }

    /* --- ANOMALIA ELÉTRICA: FADING DO BIT R ---
     * A cada 5 acessos globais, a MMU zera o bit R de uma página aleatória na RAM.
     */
    if (state->global_access_count % FADING_THRESHOLD == 0) {
        uint32_t random_vpn = rand() % num_entries;
        if (page_table[random_vpn].present) {
            page_table[random_vpn].referenced = false;
        }
    }

    /* --- VERIFICAÇÃO DE HIT / PAGE FAULT --- */
    if (!page_table[vpn].present) {
        /* Interrupção de Page Fault capturada! O SO tratará isto. */
        return false; 
    }

    /* Se chegou aqui, é um HIT de Memória */
    page_table[vpn].referenced = true;
    page_table[vpn].last_access_t = current_timestamp;
    
    if (is_write) {
        page_table[vpn].modified = true;
    }

    return true;
}

void hardware_trigger_page_out(hardware_state_t *state, uint32_t current_timestamp_ms) {
    if (state == NULL) return;

    /* Regra Térmica: Aquece 15°C se operações ocorrerem no mesmo milissegundo virtual */
    if (current_timestamp_ms == state->last_swap_ms) {
        state->swap_temperature += 15.0f;
    } else {
        /* Se mudou o MS virtual, o hardware teve um alívio parcial imediato antes da nova carga */
        uint32_t diff = current_timestamp_ms - state->last_swap_ms;
        hardware_cool_down(state, diff);
        state->swap_temperature += 12.0f; /* Menos estresse se escalonado */
    }

    state->last_swap_ms = current_timestamp_ms;

    /* Custo Base de E/S de Bloco em ciclos de clock */
    uint64_t operation_cost = 1000; 

    /* --- PENALIDADE DE THERMAL THROTTLING ---
     * Se passar de 65°C, o hardware dobra o custo em ciclos a cada 5°C adicionais.
     */
    if (state->swap_temperature > MAX_SAFE_TEMP) {
        float excess = state->swap_temperature - MAX_SAFE_TEMP;
        int penalty_multipliers = (int)(excess / 5.0f) + 1;
        operation_cost *= (1 << penalty_multipliers); /* Multiplica por 2^n */
    }

    state->total_cpu_cycles += operation_cost;
}

void hardware_cool_down(hardware_state_t *state, uint32_t elapsed_ms) {
    if (state == NULL) return;

    /* Resfriamento passivo do dispositivo de Swap: 0.5°C por milissegundo ocioso */
    float cooling = elapsed_ms * 0.5f;
    state->swap_temperature -= cooling;
    
    if (state->swap_temperature < BASE_SWAP_TEMP) {
        state->swap_temperature = BASE_SWAP_TEMP;
    }
}
