# Antikit-8 — Simulador de Gerenciamento de Memória

## Sobre o projeto

O **Antikit-8** é um simulador de gerenciamento de memória desenvolvido em linguagem C para a disciplina de Sistemas Operacionais.

O projeto tem como objetivo simular o funcionamento de mecanismos de gerenciamento de memória, incluindo tradução de endereços virtuais, paginação assimétrica, gerenciamento de memória física, substituição de páginas, swap e monitoramento de métricas.

Uma das principais características do simulador é a utilização de páginas virtuais com tamanhos diferentes, permitindo observar os efeitos da paginação sobre a utilização da memória.

## Funcionalidades

- **Tradução de endereços virtuais:** cálculo da VPN (Virtual Page Number) e do offset.
- **Paginação assimétrica:** páginas pares de 4 KB e páginas ímpares de 8 KB.
- **Gerenciamento de memória física:** simulação de uma RAM de 256 KB.
- **Page Faults:** identificação e tratamento de acessos a páginas não carregadas.
- **WSClock e Aging:** mecanismos utilizados no gerenciamento e na substituição de páginas.
- **Swap:** simulação da transferência de páginas entre memória física e armazenamento secundário, considerando condições térmicas.
- **Auditoria:** geração de checksums para verificar alterações nas estruturas de gerenciamento de memória.
- **Fading:** atualização periódica dos bits de referência.
- **Fragmentação interna:** cálculo do desperdício de espaço nas páginas alocadas.
- **Mapa ASCII:** representação visual da ocupação da memória física.
- **Métricas:** registro de acessos, Page Faults, substituições de páginas, ciclos e temperatura.

## Tecnologias utilizadas

- Linguagem C
- GCC
- Linux / WSL
- Git e GitHub

## Como executar

### 1. Clonar o repositório

```bash
git clone https://github.com/Lzaqui/projeto-Antikit-8.git
cd projeto-Antikit-8
```

### 2. Compilar

É necessário ter o GCC instalado.

```bash
gcc main.c hardware_specs.c -o antikit
```

### 3. Executar o simulador

```bash
./antikit memory_trace.txt
```

Para executar um teste específico, basta informar o caminho do arquivo de trace.

## Testes realizados

### Teste 1 — Tradução assimétrica

Verifica se a MMU calcula corretamente a página virtual e o deslocamento para endereços próximos aos limites das páginas.

| Endereço virtual | VPN esperada | Offset esperado |
|---|---|---|
| 12287 | 1 | 8191 |
| 12288 | 2 | 0 |
| 1048575 | 170 | 4095 |

Execução:

```bash
./antikit testes/mmu/teste1_traducao.txt
```

### Teste de dois processos

Verifica o gerenciamento de páginas virtuais acessadas por processos diferentes, observando os Page Faults e a alocação de blocos físicos.

```bash
./antikit testes/mmu/teste_dois_processos.txt
```

### Teste 2 — WSClock e comportamento térmico

Avalia o comportamento do simulador diante de múltiplos acessos à memória, considerando substituição de páginas, referências recentes e gerenciamento térmico do swap.

```bash
./antikit testes/wsclock/teste2_termico.txt
./antikit testes/wsclock/teste_wsclock_recente.txt
```

### Teste de auditoria e fragmentação

Avalia o registro de checksums, o mecanismo de fading e o cálculo de fragmentação interna.

```bash
./antikit testes/auditoria/teste4_1kb.txt
./antikit testes/auditoria/teste4_final.txt
./antikit testes/auditoria/teste4_fragmentacao.txt
```

## Resultados observados

Na validação da tradução assimétrica, os três endereços de teste foram identificados corretamente, incluindo o último endereço válido do espaço virtual de 1 MB.

No teste de dois processos, foram observados dois Page Faults iniciais, um para cada processo, seguidos de acessos às páginas já carregadas.

O simulador também disponibiliza métricas de ocupação da RAM, fragmentação, Page Faults por processo, ciclos de CPU e temperatura do swap.

## Integrantes

- **Lorenna Zaqui Oliveira** — MMU e validação da tradução assimétrica.
- **Luiz Fellipe de Lima Barbosa** — WSClock e Swap.
- **Miguel** — Auditoria e métricas.

## Contexto acadêmico

Projeto desenvolvido como atividade prática da disciplina de **Sistemas Operacionais**, com o propósito de aplicar conceitos de gerenciamento de memória por meio de simulação e testes.
