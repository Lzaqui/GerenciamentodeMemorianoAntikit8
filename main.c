#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "hardware_specs.h"

#define MAX_PAGES (VIRTUAL_MEMORY_SIZE / PAGE_SIZE_EVEN + 1)
#define BLOCKS (PHYSICAL_RAM_SIZE / PAGE_SIZE_EVEN)
#define AGE_LIMIT 5U
#define MAX_PROCESSES 256U
#define TOTAL_ENTRIES (MAX_PROCESSES * MAX_PAGES)

typedef struct {
    hardware_state_t hw;
    page_table_entry_t *pte;
    bool used[BLOCKS];
    uint32_t n_pages, hand, clock_ms;
    uint32_t owner[BLOCKS]; /* indice global da pagina ocupante */
    uint64_t hits, faults, evictions, dirty_outs, throttles, reads, writes, io_cycles, fading_events, lines;
    uint64_t process_faults[256];
    float max_temp, fragmentation_sum;
    uint32_t touched_bytes[TOTAL_ENTRIES];
    uint32_t checksum[TOTAL_ENTRIES];
    bool swapped[TOTAL_ENTRIES];
} simulator_t;

static uint32_t page_size(uint32_t vpn) {
    return (vpn & 1U) ? PAGE_SIZE_ODD : PAGE_SIZE_EVEN;
}

static bool translate(uint32_t addr, uint32_t *vpn, uint32_t *offset) {
    if (addr >= VIRTUAL_MEMORY_SIZE) return false;
    uint32_t start = 0;
    for (uint32_t p = 0; start < VIRTUAL_MEMORY_SIZE; ++p) {
        uint32_t size = page_size(p);
        if (addr - start < size) {
            *vpn = p;
            *offset = addr - start;
            return true;
        }
        start += size;
    }
    return false;
}

static uint32_t count_pages(void) {
    uint32_t vpn, offset;
    if (!translate(VIRTUAL_MEMORY_SIZE - 1U, &vpn, &offset)) return 0;
    return vpn + 1U;
}

static uint32_t index_of(const simulator_t *s, uint32_t pid, uint32_t vpn) {
    return pid * s->n_pages + vpn;
}

static uint32_t total_entries(const simulator_t *s) {
    return MAX_PROCESSES * s->n_pages;
}

static void init(simulator_t *s) {
    memset(s, 0, sizeof(*s));
    hardware_init(&s->hw);
    s->n_pages = count_pages();
    s->pte = calloc(total_entries(s), sizeof(*s->pte));
    if (!s->pte) { perror("calloc pte"); exit(1); }
    s->max_temp = BASE_SWAP_TEMP;
    srand(1); /* Testes reproduziveis do fading aleatorio do hardware. */
}

static int find_free(const simulator_t *s, uint32_t count) {
    for (uint32_t i = 0; i + count <= BLOCKS; ++i) {
        bool ok = true;
        for (uint32_t j = 0; j < count; ++j)
            if (s->used[i + j]) { ok = false; break; }
        if (ok) return (int)i;
    }
    return -1;
}

static uint32_t age(uint32_t now, uint32_t last) {
    return now >= last ? now - last : 0;
}

/* WSClock: segunda chance, idade, paginas limpas primeiro.
   Se todas forem sujas, retorna uma pagina suja antiga para Page Out. */
static int choose_victim(simulator_t *s) {
    int dirty_candidate = -1;
    uint32_t total = total_entries(s);
    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < total; ++i) {
            uint32_t idx = s->hand;
            s->hand = (s->hand + 1U) % total;
            page_table_entry_t *p = &s->pte[idx];
            if (!p->present) continue;
            if (p->referenced) { p->referenced = false; continue; }
            if (age(s->clock_ms, p->last_access_t) < AGE_LIMIT) continue;
            if (!p->modified) return (int)idx;
            if (dirty_candidate < 0) dirty_candidate = (int)idx;
        }
    }
    if (dirty_candidate >= 0) return dirty_candidate;
    int fallback_dirty = -1;
    for (uint32_t i = 0; i < total; ++i) {
        uint32_t idx = s->hand;
        s->hand = (s->hand + 1U) % total;
        page_table_entry_t *p = &s->pte[idx];
        if (!p->present) continue;
        if (!p->modified) return (int)idx;
        if (fallback_dirty < 0) fallback_dirty = (int)idx;
    }
    return fallback_dirty;
}

static void audit(const simulator_t *s, const char *reason) {
    uint32_t hash = 2166136261U;
    for (uint32_t i = 0; i < total_entries(s); ++i) {
        uint32_t value = (s->pte[i].present ? 0x80000000U : 0U) | s->pte[i].frame_number;
        hash = (hash ^ i) * 16777619U;
        hash = (hash ^ value) * 16777619U;
    }
    printf("[AUDITORIA] %s | checksum tabela=%08x\n", reason, hash);
}

static void evict(simulator_t *s, uint32_t idx) {
    uint32_t vpn = idx % s->n_pages;
    page_table_entry_t *p = &s->pte[idx];
    if (!p->present) return;
    if (p->modified) {
        /* Evita overflow no hardware de teste por aquecimento ilimitado. */
        if (s->hw.swap_temperature >= 50.0f) {
            hardware_cool_down(&s->hw, 80U);
            s->clock_ms += 80U;
        }
        uint64_t before = s->hw.total_cpu_cycles;
        hardware_trigger_page_out(&s->hw, s->clock_ms);
        s->io_cycles += s->hw.total_cpu_cycles - before;
        if (s->hw.swap_temperature > s->max_temp) s->max_temp = s->hw.swap_temperature;
        ++s->dirty_outs;
        if (s->hw.swap_temperature > MAX_SAFE_TEMP) ++s->throttles;
        p->modified = false;
    }
    uint32_t blocks = page_size(vpn) / PAGE_SIZE_EVEN;
    uint32_t first = p->frame_number;
    for (uint32_t j = 0; j < blocks && first + j < BLOCKS; ++j)
        s->used[first + j] = false;
    p->present = false;
    p->referenced = false;
    s->swapped[idx] = true;
    ++s->evictions;
    audit(s, "PAGE OUT / remocao");
}

/* A pagina virtual tem uma assinatura de dados simulada; ela permanece
   associada a VPN mesmo quando a pagina sai da RAM. */
static uint32_t checksum_value(uint32_t vpn, uint32_t value) {
    uint32_t x = 2166136261U;
    for (int i = 0; i < 4; ++i) {
        x = (x ^ ((vpn >> (i * 8)) & 255U)) * 16777619U;
        x = (x ^ ((value >> (i * 8)) & 255U)) * 16777619U;
    }
    return x;
}

static bool ensure_resident(simulator_t *s, uint32_t pid, uint32_t vpn) {
    uint32_t idx = index_of(s, pid, vpn);
    if (s->pte[idx].present) return true;
    uint32_t needed = page_size(vpn) / PAGE_SIZE_EVEN;
    int first = find_free(s, needed);
    uint32_t attempts = 0;
    while (first < 0 && attempts < total_entries(s)) {
        int victim = choose_victim(s);
        if (victim < 0) return false;
        evict(s, (uint32_t)victim);
        ++attempts;
        first = find_free(s, needed);
    }
    if (first < 0) return false;
    for (uint32_t j = 0; j < needed; ++j)
        s->used[first + (int)j] = true;
    page_table_entry_t *p = &s->pte[idx];
    p->frame_number = (uint32_t)first;
    p->present = true;
    p->referenced = false;
    p->modified = false;
    p->last_access_t = s->clock_ms;
    audit(s, "PAGE IN / mapeamento");
    return true;
}

static bool access_address(simulator_t *s, uint32_t addr, bool write, uint32_t timestamp, uint32_t pid, bool verbose) {
    uint32_t vpn, offset;
    if (!translate(addr, &vpn, &offset)) {
        if (verbose) printf("Endereco invalido: %u\n", addr);
        return false;
    }
    s->clock_ms = timestamp > s->clock_ms ? timestamp : s->clock_ms;
    if (write) ++s->writes; else ++s->reads;
    uint64_t next_access = s->hw.global_access_count + 1U;
    bool hit = mmu_access_memory(&s->hw, s->pte + index_of(s, pid, 0), s->n_pages,
                                 addr, write, s->clock_ms);
    if (next_access % FADING_THRESHOLD == 0U) {
        ++s->fading_events;
        printf("[FADING] acesso global %llu: MMU executou tentativa de zerar R aleatorio\n", (unsigned long long)next_access);
        audit(s, "apos fading");
    }
    if (hit) {
        if (write) {
            uint32_t idx = index_of(s, pid, vpn);
            s->checksum[idx] = checksum_value(vpn, s->checksum[idx] + addr + 1U);
        }
        ++s->hits;
        if (verbose) printf("HIT: VPN=%u offset=%u\n", vpn, offset);
        return true;
    }
    ++s->faults;
    if (pid < 256U) ++s->process_faults[pid];
    if (verbose) printf("PAGE FAULT: VPN=%u offset=%u\n", vpn, offset);
    if (!ensure_resident(s, pid, vpn)) {
        if (verbose) printf("Falha: RAM sem espaco contiguo recuperavel.\n");
        return false;
    }
    /* Reexecuta a operacao pela MMU, para atualizar R, M e timestamp. */
    next_access = s->hw.global_access_count + 1U;
    bool retry = mmu_access_memory(&s->hw, s->pte + index_of(s, pid, 0), s->n_pages,
                                   addr, write, s->clock_ms);
    if (next_access % FADING_THRESHOLD == 0U) {
        ++s->fading_events;
        printf("[FADING] acesso global %llu: MMU executou tentativa de zerar R aleatorio\n", (unsigned long long)next_access);
        audit(s, "apos fading (retry)");
    }
    if (retry) ++s->hits;
    if (verbose) printf("%s: VPN=%u bloco=%u\n",
                        retry ? "HIT apos carregamento" : "ERRO no retry",
                        vpn, s->pte[index_of(s, pid, vpn)].frame_number);
    if (retry && write) {
        s->checksum[index_of(s, pid, vpn)] = checksum_value(vpn, s->checksum[index_of(s, pid, vpn)] + addr + 1U);
    } else if (retry && !write && s->checksum[index_of(s, pid, vpn)] == 0U) {
        s->checksum[index_of(s, pid, vpn)] = checksum_value(vpn, 0U);
    }
    return retry;
}

static void print_stats(const simulator_t *s) {
    uint32_t occupied = 0, free_runs = 0, largest_run = 0, run = 0;
    for (uint32_t i = 0; i < BLOCKS; ++i) {
        if (s->used[i]) {
            ++occupied;
            if (run > largest_run) largest_run = run;
            run = 0;
        } else {
            if (i == 0 || s->used[i - 1U]) ++free_runs;
            ++run;
        }
    }
    if (run > largest_run) largest_run = run;
    printf("\n=== METRICAS ===\n");
    printf("Acessos solicitados pelo trace: %llu\n", (unsigned long long)s->lines);
    printf("Linhas do trace: %llu\n", (unsigned long long)s->lines);
    printf("Paginas virtuais: %u | Blocos RAM: %u\n", s->n_pages, (unsigned)BLOCKS);
    printf("Blocos ocupados: %u | Livres: %u\n", occupied, (unsigned)BLOCKS - occupied);
    printf("Regioes livres: %u | Maior regiao livre: %u blocos\n", free_runs, largest_run);
    printf("Hits MMU (inclui retries): %llu | Page Faults: %llu\n",
           (unsigned long long)s->hits, (unsigned long long)s->faults);
    printf("Evictions: %llu | Page Outs sujos: %llu\n",
           (unsigned long long)s->evictions, (unsigned long long)s->dirty_outs);
    printf("Leituras: %llu | Escritas: %llu\n",
           (unsigned long long)s->reads, (unsigned long long)s->writes);
    printf("Acessos globais MMU: %llu | Ciclos CPU: %llu\n",
           (unsigned long long)s->hw.global_access_count,
           (unsigned long long)s->hw.total_cpu_cycles);
    printf("Temperatura maxima: %.1f C | Ciclos gastos em E/S: %llu\n",
           s->max_temp, (unsigned long long)s->io_cycles);
    printf("Fragmentacao interna media (amostras a cada 10 linhas): %.2f%%\n",
           s->lines / 10 ? s->fragmentation_sum / (s->lines / 10) : 0.0);
    printf("Page Faults por processo:\n");
    for (uint32_t p = 0; p < 256; ++p) if (s->process_faults[p])
        printf("  PID %u: %llu\n", p, (unsigned long long)s->process_faults[p]);
    printf("Temperatura Swap: %.1f C | Page Outs acima de 65 C: %llu\n",
           s->hw.swap_temperature, (unsigned long long)s->throttles);
}

static void print_map(const simulator_t *s);
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "memory_trace.txt";
    FILE *file = fopen(path, "r");
    if (!file) { perror(path); return 1; }
    simulator_t s;
    init(&s);
    printf("=== ANTIKIT-8: PROCESSAMENTO DO TRACE ===\n");
    printf("Arquivo: %s | VPNs: %u | RAM: %u KB\n", path, s.n_pages, PHYSICAL_RAM_SIZE / 1024);
    printf("=== TESTE DOS LIMITES ===\n");
    uint32_t addresses[] = {12287, 12288, 1048575};
    for (size_t i = 0; i < 3; ++i) {
        uint32_t vpn, off;
        translate(addresses[i], &vpn, &off);
        printf("Endereco %u -> VPN %u, offset %u\n", addresses[i], vpn, off);
    }
    char line[256];
    uint32_t previous_timestamp = 0;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        unsigned timestamp, pid, address;
        char op;
        if (sscanf(line, "%u %u %c %u", &timestamp, &pid, &op, &address) != 4 ||
            (op != 'R' && op != 'W') || address >= VIRTUAL_MEMORY_SIZE || pid >= 256) {
            fprintf(stderr, "Linha invalida: %s", line);
            fclose(file);
            free(s.pte);
            return 1;
        }
        if (timestamp < previous_timestamp) {
            fprintf(stderr, "Trace fora de ordem temporal\n");
            fclose(file);
            free(s.pte);
            return 1;
        }
        previous_timestamp = timestamp;
        uint32_t vpn, offset;
        translate(address, &vpn, &offset);
        uint32_t consumed = offset + 1U;
        if (consumed > s.touched_bytes[index_of(&s, pid, vpn)]) s.touched_bytes[index_of(&s, pid, vpn)] = consumed;
        if (!access_address(&s, address, op == 'W', timestamp, pid, true)) {
            fprintf(stderr, "Falha irrecuperavel na linha %llu\n", (unsigned long long)(s.lines + 1));
            fclose(file);
            free(s.pte);
            return 1;
        }
        ++s.lines;
        if (s.lines % 10 == 0) {
            print_map(&s);
            uint32_t allocated = 0, wasted = 0;
            for (uint32_t idx = 0; idx < total_entries(&s); ++idx) if (s.pte[idx].present) {
                uint32_t p = idx % s.n_pages;
                allocated += page_size(p);
                wasted += page_size(p) - s.touched_bytes[idx];
            }
            s.fragmentation_sum += allocated ? 100.0f * wasted / allocated : 0.0f;
            printf("[AMOSTRA] linhas=%llu temperatura=%.1f ciclos_io=%llu\n",
                (unsigned long long)s.lines, s.hw.swap_temperature,
                (unsigned long long)s.io_cycles);
        }
    }
    fclose(file);
    print_map(&s);
    print_stats(&s);
    free(s.pte);
    return 0;
}

static void print_map(const simulator_t *s) {
    char map[BLOCKS];

    for (uint32_t i = 0; i < BLOCKS; ++i) {
        map[i] = '.';
    }

    uint32_t allocated = 0;
    uint32_t wasted = 0;

    for (uint32_t idx = 0; idx < total_entries(s); ++idx) {
        if (!s->pte[idx].present) continue;
        uint32_t p = idx % s->n_pages;

        uint32_t size = page_size(p);
        uint32_t touched = s->touched_bytes[idx];

        if (touched > size) touched = size;

        allocated += size;
        wasted += size - touched;

        uint32_t first = s->pte[idx].frame_number;
        uint32_t blocks = size / PAGE_SIZE_EVEN;

        for (uint32_t b = 0; b < blocks; ++b) {
            uint32_t index = first + b;
            if (index >= BLOCKS) break;

            uint32_t block_start = b * PAGE_SIZE_EVEN;
            map[index] = touched > block_start ? 'U' : 'F';
        }
    }

    printf("\n=== MAPA ASCII DA RAM (cada caractere = 4 KB) ===\n");

    for (uint32_t i = 0; i < BLOCKS; ++i) {
        putchar(map[i]);
        if ((i + 1U) % 16U == 0U) putchar('\n');
    }

    printf("Legenda: U = regiao acessada | F = regiao alocada nao acessada | . = livre\n");

    printf("Fragmentacao interna estimada: %.2f%% (%u / %u bytes)\n",
           allocated ? 100.0 * wasted / allocated : 0.0,
           wasted,
           allocated);
}
