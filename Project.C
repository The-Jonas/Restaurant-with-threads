#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L // Necessário para clock_gettime
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>                            // Para threads
#include <semaphore.h>                          // Para semáforos 
#include <unistd.h>                             // Para sleep
#include <time.h>                               // Para o timeout e o rand()
#include <errno.h>                              // Para ETIMEOUT

/* Para compilar e rodar o projeto, use:
gcc Project.c -o restaurante.exe -pthread (ou variante de compilação)
    E
./restaurante.exe
*/

/*=======================================================*
 *=========== VARIÁVEIS GLOBAIS E DEFINIÇÕES ============*
 *=======================================================*/

// Inputs do usuário
int MAX_CLIENTES_POR_DIA;
int MAX_MESAS_RESTAURANTE;
int MAX_ESTOQUE_PRATO;

#define N_GARCONS 2                                                     // Número de garçons no restaurante
#define N_COZINHEIROS 2                                                 // Número de cozinheiros no restaurante
#define TAMANHO_FILA 50                                                 // Tamanho máximo da fila de pedidos
#define TOTAL_PRATOS 5                                                  // Total de pratos/receitas diferentes no restaurante
#define TIMEOUT_CLIENTE_ESPERA 3                                        // Cliente espera 5 segundos max
                                                                        
// Enumerador para os dias da semana                            

typedef enum {
   DOMINGO = 1,
   SEGUNDA,
   TERCA,
   QUARTA,
   QUINTA,
   SEXTA,
   SABADO
} DiaDaSemana;
DiaDaSemana dia_atual;                                                  // Variável global para controlar o dia atual

// --- Controles de Sincronização ---
pthread_mutex_t mutex_print;                                            // Mutex pra controlar a impressão no console 
pthread_mutex_t mutex_restaurante;                                      // Protege o estado do restaurante
pthread_mutex_t mutex_fila_chamados;
pthread_mutex_t mutex_fila_pendentes;
pthread_mutex_t mutex_fila_prontos;
pthread_mutex_t mutex_estoque;
pthread_mutex_t mutex_rand_seed;                                        // Mutex para proteger a semente
pthread_mutex_t mutex_lucro;

pthread_cond_t cond_cliente_chegou;                                     // Cliente sinaliza pro gestor
pthread_cond_t cond_mesa_disponivel;                                    // Gestor sinaliza pro cliente
pthread_cond_t cond_estoquista_precisa_repor;                           // Condicionais para
pthread_cond_t cond_estoque_reposto;                                    // o estoquista verificar
pthread_cond_t cond_todos_clientes_sairam;                              // Condição pro gerente esperar o fim do expediente

sem_t sem_clientes_chamando;                                            // Cliente -> Garçom
sem_t sem_pedidos_pendentes;                                            // Garçom -> Cozinheiro
sem_t sem_pedidos_prontos;                                              // Cozinheiro -> Garçom
sem_t sem_limpeza_necessaria;                                           // Cliente/Garçom -> Limpeza

// -- Estado do Restaurante (reiniciado todo dia) ---
int mesas_criadas;                                                      // Começa com 0
int mesas_ocupadas;
int clientes_esperando;
int restaurante_fechado;                                                // 0 = ABERTO,  1 = FECHADO

int estoque[TOTAL_PRATOS];

int precos_pratos[TOTAL_PRATOS];
int lucro_dia;
int lucro_total_semana = 0;
int clientes_que_sairam_total;

/*=======================================================*
 *=========== STRUCTS (Estruturas de Dados) =============*
 *=======================================================*/

// Struct para um pedido
typedef struct {
    int id_cliente;
    int id_prato;                                                       // 0 a (TOTAL_PRATOS - 1)
    pthread_cond_t cond_prato_entregue;                                 // Cliente espera aqui
    int mesa_id;                                                        // Futuro uso para limpeza
} Pedido;

// Filas de Pedidos (usando o ponteiro para a Struct Pedido)
Pedido* fila_chamados[TAMANHO_FILA];
int count_chamados = 0;

Pedido* fila_pedidos_pendentes[TAMANHO_FILA];
int count_pendentes = 0;

Pedido* fila_pedidos_prontos[TAMANHO_FILA];
int count_prontos = 0;


 /*=======================================================*
  *========== PROTÓTIPOS DAS FUNÇÕES (THREADS) ===========*
  *=======================================================*/

// Cada entidade será uma thread, e cada thread executa uma função
void* gerente_do_dia_func(void *arg);
void* cliente_func(void *arg);
void* garcom_func(void *arg);
void* cozinheiro_func(void *arg);
void* estoquista_func(void *arg);
void* gestor_mesas_func(void *arg);
void* responsavel_limpeza_func(void *arg);
void* timer_restaurante_func(void *arg);

// Função utilitária para imprimir com mutex
void print_safe(char* msg) {
    pthread_mutex_lock(&mutex_print);
    printf("%s", msg);
    pthread_mutex_unlock(&mutex_print);
}

// Função thread-safe para gerar números aléatorios
int rand_safe(int min_val, int max_val) {
    pthread_mutex_lock(&mutex_rand_seed);
    static unsigned int seed = 0;
    if (seed == 0) {
        seed = (unsigned int)time(NULL) + pthread_self();
    }
    
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    pthread_mutex_unlock(&mutex_rand_seed);
    return min_val + (seed % (max_val - min_val + 1));
}

 /*=======================================================*
  *======================== MAIN =========================*
  *=======================================================*/

int main() {
    srand(time(NULL));                                                  // Semente para números aleatórios

    // 1. Obter os inputs do usuário
    printf("Digite o número maximo de clientes por dia: ");
    scanf("%d", &MAX_CLIENTES_POR_DIA);
    printf("Digite o número máximo de mesas do restaurante: ");
    scanf("%d", &MAX_MESAS_RESTAURANTE);
    printf("Digite o estoque máximo de cada prato: ");
    scanf("%d", &MAX_ESTOQUE_PRATO);

    printf("\n---Iniciando simulação do Restaurante por uma semana---\n");

    // 2. Inicializar Mutexes, Condicionais e Semáforos
    pthread_mutex_init(&mutex_print, NULL);
    pthread_mutex_init(&mutex_restaurante, NULL);
    pthread_mutex_init(&mutex_fila_chamados, NULL);
    pthread_mutex_init(&mutex_fila_pendentes, NULL);
    pthread_mutex_init(&mutex_fila_prontos, NULL);
    pthread_mutex_init(&mutex_estoque, NULL);
    pthread_mutex_init(&mutex_rand_seed, NULL);
    pthread_mutex_init(&mutex_lucro, NULL);

    pthread_cond_init(&cond_cliente_chegou, NULL);
    pthread_cond_init(&cond_mesa_disponivel, NULL);
    pthread_cond_init(&cond_estoquista_precisa_repor, NULL);
    pthread_cond_init(&cond_estoque_reposto, NULL);
    pthread_cond_init(&cond_todos_clientes_sairam, NULL);

    // *** NOVO BLOCO: GERAR E IMPRIMIR PREÇOS ***
    printf("\n=================================================\n");
    printf("Gerando o cardapio de precos para a semana (de 10 a 50 R$):\n");
    for (int i = 0; i < TOTAL_PRATOS; i++) {
        // Usamos sua função rand_safe para gerar preços de 10 a 50
        precos_pratos[i] = rand_safe(10, 50); 
        printf("  > Prato %d: %d R$\n", i, precos_pratos[i]);
    }

    // Inicializa o estoque, uma única vez para os 7 dias
    for (int i = 0; i < TOTAL_PRATOS; i++) {
        estoque[i] = MAX_ESTOQUE_PRATO;
    }
    
    // 3. Loop principal dos 7 dias
    for (dia_atual = DOMINGO; dia_atual <= SABADO; dia_atual++) {

        char buffer[100];
        sprintf(buffer, "\n==================== DIA %d ====================\n", dia_atual);
        print_safe(buffer);
        
        // O restaurante não funciona no fim de semana (Domingo e Sábado)
        if (dia_atual == DOMINGO || dia_atual == SABADO) {
            print_safe("Restaurante fechado (Fim de Semana).\n");
            sleep(2);
            continue;                                           
        }

        // --- Início do Dia de Trabalho --

        // Todos começam em 0, pois não há tarefas no início
        sem_init(&sem_clientes_chamando, 0, 0);
        sem_init(&sem_pedidos_pendentes, 0, 0);
        sem_init(&sem_pedidos_prontos, 0, 0);
        sem_init(&sem_limpeza_necessaria, 0, 0);
        
        // A primeira thread a ser chamada é gerente_do_dia
        pthread_t tif_gerente_dia;
        pthread_create(&tif_gerente_dia, NULL, gerente_do_dia_func, NULL);  

        // A main espera o gerente do dia terminar
        // Ele só termina quando o dia acaba (restaurante fecha e todos os clientes saem)
        pthread_join(tif_gerente_dia, NULL);

        // Destrói semáforos A CADA DIA
        sem_destroy(&sem_clientes_chamando);
        sem_destroy(&sem_pedidos_pendentes);
        sem_destroy(&sem_pedidos_prontos);
        sem_destroy(&sem_limpeza_necessaria);

        sprintf(buffer, "--- Fim do Dia %d ---\n", dia_atual);
        print_safe(buffer);
    }

    // 4. Fim da simulação
    printf("\n=================================================\n");
    printf("========= LUCRO TOTAL DA SEMANA: %d R$ ========\n", lucro_total_semana);
    printf("======== Simulacao de 7 dias encerrada. =========\n");
    printf("=================================================\n");

    // 5. Destruir TUDO
    pthread_mutex_destroy(&mutex_print);
    pthread_mutex_destroy(&mutex_restaurante);
    pthread_mutex_destroy(&mutex_fila_chamados);
    pthread_mutex_destroy(&mutex_fila_pendentes);
    pthread_mutex_destroy(&mutex_fila_prontos);
    pthread_mutex_destroy(&mutex_estoque);
    pthread_mutex_destroy(&mutex_rand_seed);
    pthread_mutex_destroy(&mutex_lucro);
    
    pthread_cond_destroy(&cond_cliente_chegou);
    pthread_cond_destroy(&cond_mesa_disponivel);
    pthread_cond_destroy(&cond_estoquista_precisa_repor);
    pthread_cond_destroy(&cond_estoque_reposto);
    pthread_cond_destroy(&cond_todos_clientes_sairam);

    return 0;
}

 /*=======================================================*
  *============== IMPLEMENTAÇÃO DAS FUNÇÕES ==============*
  *=======================================================*/

/*================================
======== GERENTE DO DIA ==========
================================*/

void* gerente_do_dia_func(void* arg) {
    char buffer[200];
    sprintf(buffer, "[⊛ GERENTE DO DIA] Bom dia! Iniciando o dia %d.\n", dia_atual);
    print_safe(buffer);

    // TODO: 
    // 1. Reinicializar variáveis do dia
    pthread_mutex_lock(&mutex_restaurante);
    mesas_criadas = 0;
    mesas_ocupadas = 0;
    clientes_esperando = 0;
    restaurante_fechado = 0;
    count_chamados = 0;
    count_pendentes = 0;
    count_prontos = 0;
    pthread_mutex_unlock(&mutex_restaurante);

    pthread_mutex_lock(&mutex_lucro);
    lucro_dia = 0;
    pthread_mutex_unlock(&mutex_lucro);

    // 2. Acordar as threads "fixas"
    pthread_t tid_gestor_mesas;
    pthread_t tid_limpeza;
    pthread_t tid_estoquista;
    pthread_t tid_timer;
    pthread_t tid_garcons[N_GARCONS];
    pthread_t tid_cozinheiros[N_COZINHEIROS];
    int i;                                                                                          // Usando i como ID

    int tempo_dia_segundos = 15;                                                                    // O tempo que o restaurante ficará aberto
    pthread_create(&tid_timer, NULL, timer_restaurante_func, (void*)(intptr_t)tempo_dia_segundos);
    
    //(Vamos criar garçons, cozinheiros, etc aqui...)
    
    pthread_create(&tid_gestor_mesas, NULL, gestor_mesas_func, NULL);
    pthread_create(&tid_limpeza, NULL, responsavel_limpeza_func, NULL);
    pthread_create(&tid_estoquista, NULL, estoquista_func, (void*)(intptr_t)1);

    for (i = 0; i < N_GARCONS; i++) {
        pthread_create(&tid_garcons[i], NULL, garcom_func, (void*)(intptr_t)(i+1));
    }

    for (i = 0; i < N_COZINHEIROS; i++) {
        pthread_create(&tid_cozinheiros[i], NULL, cozinheiro_func, (void*)(intptr_t)(i+1));
    }
    
    // 3. Loop para criar threads de clientes
    pthread_t clientes_threads[MAX_CLIENTES_POR_DIA];
    for (i = 0; i < MAX_CLIENTES_POR_DIA; i++) {
        // Se o restaurante fechar, parar de criar clientes
        pthread_mutex_lock(&mutex_restaurante);
        if (restaurante_fechado) {
            pthread_mutex_unlock(&mutex_restaurante);
            break;
        }
        pthread_mutex_unlock(&mutex_restaurante);

        // Passar o ID do cliente para a thread (alocação de memória)
        int* cliente_id = malloc(sizeof(int));
        *cliente_id = i + 1;
        pthread_create(&clientes_threads[i], NULL, cliente_func, (void*)cliente_id);

        sleep(rand_safe(2, 3));                                                                      // Timer random de 2 a 3 segundos
    }

    // Salva o número total de clientes que *tentarão* ser atendidos
    int clientes_criados_total = i;

    // 4. ESPERA O DIA ACABAR (pela Condição 1 OU 2)
    pthread_mutex_lock(&mutex_restaurante);
    
    sprintf(buffer, "[⊛ GERENTE DO DIA] Esperando o expediente acabar (atendi %d clientes)...\n", clientes_criados_total);
    print_safe(buffer);

    while(1) {
        // CONDIÇÃO 1: Todos os clientes do dia foram atendidos e restaurante ainda não fechou
        if (clientes_que_sairam_total == clientes_criados_total && !restaurante_fechado) {
            sprintf(buffer, "[⊛ GERENTE DO DIA] Todos os %d clientes foram atendidos! Fechando o restaurante.\n", clientes_criados_total);
            print_safe(buffer);
            break; 
        }
        
        // CONDIÇÃO 2: O timer acabou E os de dentro saíram 
        if (restaurante_fechado && mesas_ocupadas == 0) {
            sprintf(buffer, "[⊛ GERENTE DO DIA] O restaurante fechou e todos os clientes sairam! Encerrando o dia.\n");
            print_safe(buffer);
            break;
        }
        
        // Se nenhuma condição for atendida, dorme e espera um sinal
        pthread_cond_wait(&cond_todos_clientes_sairam, &mutex_restaurante);
    }

    // 5. O dia acabou.
    // Garante que o timer pare (caso o dia tenha acabado pela Condição 1)
    restaurante_fechado = 1;

    // Acorda todos os threads esperando para que vejam que fechou
    pthread_cond_broadcast(&cond_cliente_chegou);
    pthread_cond_broadcast(&cond_mesa_disponivel);
    
    // 6. Depois manda toda a staff ir embora
    // Acorda todos os staff que estão "dormindo"
    sem_post(&sem_clientes_chamando);
    sem_post(&sem_pedidos_pendentes);
    sem_post(&sem_pedidos_prontos);
    sem_post(&sem_limpeza_necessaria);

    pthread_mutex_unlock(&mutex_restaurante);

    // Acorda o Estoquista e Cozinheiros que possam estar dormindo
    pthread_mutex_lock(&mutex_estoque);
    pthread_cond_broadcast(&cond_estoquista_precisa_repor); 
    pthread_cond_broadcast(&cond_estoque_reposto);
    pthread_mutex_unlock(&mutex_estoque);
    
    // 7. Esperar staff terminar
    // (O staff só vai terminar quando virem a flag 'restaurante_fechado'
    // E não tiver mais trabalho pendente)
    pthread_join(tid_gestor_mesas, NULL);
    pthread_join(tid_limpeza, NULL);
    pthread_join(tid_estoquista, NULL);

    for (i = 0; i < N_GARCONS; i++) {
        pthread_join(tid_garcons[i], NULL);
    }
    for (i = 0; i < N_COZINHEIROS; i++) {
        pthread_join(tid_cozinheiros[i], NULL);
    }

    // Salva e imprime o lucro do dia 
    pthread_mutex_lock(&mutex_lucro);
    lucro_total_semana += lucro_dia;
    sprintf(buffer, "[⊛ GERENTE DO DIA] Encerrando o dia %d. (Lucro do dia: %d R$)\n", dia_atual, lucro_dia);
    pthread_mutex_unlock(&mutex_lucro);
    print_safe(buffer);
    
    pthread_exit(NULL);
}

/*================================
=========== CLIENTE ==============
================================*/

void* cliente_func(void* arg) {
    int id = *(int*)arg;
    free(arg);
    char buffer[200];
    int id_prato_pedido;

    sprintf(buffer, "[● CLIENTE %d] Chegou ao restaurante.\n", id);
    print_safe(buffer);
    pthread_mutex_lock(&mutex_restaurante);

    // 1. Verifica se o restaurante já tá fechado
    if (restaurante_fechado) {
        sprintf(buffer, "[● CLIENTE %d] Restaurante ja esta fechado. Indo embora.\n", id);
        print_safe(buffer);

        clientes_que_sairam_total++;
        pthread_cond_broadcast(&cond_todos_clientes_sairam);

        pthread_mutex_unlock(&mutex_restaurante);
        pthread_exit(NULL);
    }

    // 2. Tenta pegar uma mesa
    if (mesas_ocupadas == mesas_criadas) {
        // 2a. Verifica se o restaurante está LOTADO (não pode colocar mais mesas)
        if (mesas_criadas == MAX_MESAS_RESTAURANTE) {
            sprintf(buffer, "[● CLIENTE %d] Restaurante lotado. Vou esperar por uma vaga.\n", id);
            print_safe(buffer);
        } else {
            sprintf(buffer, "[● CLIENTE %d] Sem mesas. Vou esperar adicionarem uma.\n", id);
            print_safe(buffer);
        }

        clientes_esperando++;
        // Sinaliza o Gestor de Mesas
        pthread_cond_signal(&cond_cliente_chegou);

        // 2b. Configura o TIMEOUT
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += TIMEOUT_CLIENTE_ESPERA;

        int wait_result = 0;
        // Espera ATÉ (mesa livre) OU (timeout) OU (restaurante fechar)
        while (mesas_ocupadas == mesas_criadas && wait_result == 0 && !restaurante_fechado) {
            wait_result = pthread_cond_timedwait(&cond_mesa_disponivel, &mutex_restaurante, &timeout);
        }
        clientes_esperando--;

        // 2c. Analisa o resultado da espera
        if (wait_result == ETIMEDOUT) {
            sprintf(buffer, "[● CLIENTE %d] Cansei de esperar e FUI EMBORA.\n", id);
            print_safe(buffer);

            clientes_que_sairam_total++;
            pthread_cond_broadcast(&cond_todos_clientes_sairam);

            pthread_mutex_unlock(&mutex_restaurante);
            pthread_exit(NULL);
        }

        if (restaurante_fechado) {
            sprintf(buffer, "[● CLIENTE %d] Restaurante fechou enquanto eu esperava. Indo embora.\n", id);
            print_safe(buffer);

            clientes_que_sairam_total++;
            pthread_cond_broadcast(&cond_todos_clientes_sairam);

            pthread_mutex_unlock(&mutex_restaurante);
            pthread_exit(NULL);
        }

        // Se saiu do loop e não deu timeout/fechou, é porque tem vaga!
        sprintf(buffer, "[● CLIENTE %d] Consegui uma mesa apos esperar!\n", id);
        print_safe(buffer);
    } else {
        sprintf(buffer, "[● CLIENTE %d] Sentei-me imediatamente.\n", id);
        print_safe(buffer);
    }

    // 3. Conseguiu a mesa (seja direto ou esperando)
    mesas_ocupadas++;

    // --- AQUI COMEÇA A ETAPA: FAZER PEDIDO ---
    
    // 3a. Cria o pedido
    Pedido* meu_pedido = malloc(sizeof(Pedido));
    meu_pedido->id_cliente = id;
    meu_pedido->id_prato = rand_safe(0, TOTAL_PRATOS - 1);                                                   // Escolhe um prato aleátorio
    id_prato_pedido = meu_pedido->id_prato; 
    pthread_cond_init(&meu_pedido->cond_prato_entregue, NULL);

    sprintf(buffer, "[● CLIENTE %d] Sentei e vou chamar o garcom (pedir prato %d).\n", id, meu_pedido->id_prato);
    print_safe(buffer);

    // 3b. Adiciona na fila de chamados
    pthread_mutex_lock(&mutex_fila_chamados);
    fila_chamados[count_chamados++] = meu_pedido;
    pthread_mutex_unlock(&mutex_fila_chamados);

    // 3c. Acorda um garçom
    sem_post(&sem_clientes_chamando);

    // 3d. Espera o prato ser entregue
    while (meu_pedido->id_cliente != -1) {                                                          // -1 significa "Entregue" (lógica do garçom)
        pthread_cond_wait(&meu_pedido->cond_prato_entregue, &mutex_restaurante);
    }

    // 4. Comer
    sprintf(buffer, "[● CLIENTE %d] Recebi meu prato! Comendo...\n", id);
    print_safe(buffer);

    pthread_mutex_unlock(&mutex_restaurante);                                                       // Agora que a interação com o garçom acabou, solta o mutex 

    sleep(rand_safe(3, 6));                                                                         // Simula tempo comendo
    pthread_cond_destroy(&meu_pedido->cond_prato_entregue); 
    free(meu_pedido);                                                                               // Libera a memória do pedido
    
    // 5. Pagar e sair do restaurante

    pthread_mutex_lock(&mutex_restaurante);

    pthread_mutex_lock(&mutex_lucro);
    lucro_dia += precos_pratos[id_prato_pedido]; // Usa o ID salvo
    pthread_mutex_unlock(&mutex_lucro);


    // 50% chance de sujar a mesa após terminar de comer
    if (rand_safe(0, 1) == 0) {
        // 5a. MESA LIMPA 
        mesas_ocupadas--;
        sprintf(buffer, "[● CLIENTE %d] Terminei, paguei e liberei a mesa (limpa). (Vagas agora: %d)\n", id, mesas_criadas - mesas_ocupadas); 
        print_safe(buffer);

        // Libera a vaga
        pthread_cond_signal(&cond_cliente_chegou);
        pthread_cond_broadcast(&cond_mesa_disponivel);

        // Incrementa e sinaliza
        clientes_que_sairam_total++;
        pthread_cond_broadcast(&cond_todos_clientes_sairam);

        // Verifica se é o último cliente
        if (restaurante_fechado && mesas_ocupadas == 0){
            sprintf(buffer, "[● CLIENTE %d] Fui o ultimo a sair com o restaurante fechado! Avisando o gerente.\n", id);
            print_safe(buffer);
            pthread_cond_broadcast(&cond_todos_clientes_sairam);
        }

    } else {
        // 5b. MESA SUJA
        // A MESA CONTINUA OCUPADA (pela sujeira)
        sprintf(buffer, "[● CLIENTE %d] Terminei, paguei e SUJEI a mesa! Avisando os funcionários...\n", id);
        print_safe(buffer);

        // Acorda responsável pela limpeza
        sem_post(&sem_limpeza_necessaria);

        // (A mesa continua ocupada até a limpeza terminar)
    }

    pthread_mutex_unlock(&mutex_restaurante);
    pthread_exit(NULL);
}

/*================================
============= GARÇOM =============
================================*/

void* garcom_func(void* arg) {
    int id = (intptr_t)arg;
    char buffer[200];
    sprintf(buffer, "[► GARCOM %d] Pronto para atender.\n", id);
    print_safe(buffer);

    while (1) {
        // 1. Espera um cliente chamar OU um prato ficar pronto
        sem_wait(&sem_clientes_chamando);
        
        pthread_mutex_lock(&mutex_fila_chamados);
        if (restaurante_fechado && count_chamados == 0) {
            pthread_mutex_unlock(&mutex_fila_chamados);
            sem_post(&sem_clientes_chamando);
            break;
        }

        // 2. Pega o pedido da fila de chamados
        Pedido* pedido_anotado = fila_chamados[--count_chamados];
        pthread_mutex_unlock(&mutex_fila_chamados);

        sprintf(buffer, "[► GARCOM %d] Anotando pedido do [CLIENTE %d] (prato %d).\n", id, pedido_anotado->id_cliente, pedido_anotado->id_prato);
        print_safe(buffer);

        // 3. Leva o pedido para a cozinha (fila de pendentes)
        pthread_mutex_lock(&mutex_fila_pendentes);
        fila_pedidos_pendentes[count_pendentes++] = pedido_anotado;
        pthread_mutex_unlock(&mutex_fila_pendentes);

        // 4. Acorda um cozinheiro
        sem_post(&sem_pedidos_pendentes);

        // 5. Espera um prato ficar pronto (qualquer prato)
        sem_wait(&sem_pedidos_prontos);

        // Verifica se fechou (de novo)
        pthread_mutex_lock(&mutex_fila_prontos);
        if (restaurante_fechado && count_prontos == 0) {
            pthread_mutex_unlock(&mutex_fila_prontos);
            sem_post(&sem_pedidos_prontos); // Acorda outro
            break;
        }

        // 6. Pega o prato pronto da fila
        Pedido* prato_pronto = fila_pedidos_prontos[--count_prontos];
        pthread_mutex_unlock(&mutex_fila_prontos);

        sprintf(buffer, "[► GARCOM %d] Entregando prato %d para [CLIENTE %d].\n", id, prato_pronto->id_prato, prato_pronto->id_cliente);
        print_safe(buffer);

        // 7. Acorda o cliente específico
        pthread_mutex_lock(&mutex_restaurante);
        prato_pronto->id_cliente = -1;                                              // Sinaliza que foi entregue
        pthread_cond_signal(&prato_pronto->cond_prato_entregue);
        pthread_mutex_unlock(&mutex_restaurante);

        // TODO: Receber pagamento e lidar com limpeza
    }

    sprintf(buffer, "[► GARCOM %d] Encerrando turno.\n", id);
    print_safe(buffer);
    pthread_exit(NULL);
}

/*================================
========== COZINHEIRO ============
================================*/

void* cozinheiro_func(void* arg) {
    int id = (intptr_t)arg;
    char buffer[200];
    sprintf(buffer, "[▲ COZINHEIRO %d] Pronto para cozinhar.\n", id);
    print_safe(buffer);

    while (1) {
        // 1. Espera um pedido do garçom
        sem_wait(&sem_pedidos_pendentes);

        // Verifica se o restaurante fechou
        pthread_mutex_lock(&mutex_fila_pendentes);
        if (restaurante_fechado && count_pendentes == 0) {
            pthread_mutex_unlock(&mutex_fila_pendentes);
            sem_post(&sem_pedidos_pendentes);                                   // Acorda outro
            break;
        }

        // 2. Pega o pedido da fila
        Pedido* pedido_fazer = fila_pedidos_pendentes[--count_pendentes];
        pthread_mutex_unlock(&mutex_fila_pendentes);

        sprintf(buffer, "[▲ COZINHEIRO %d] Recebi pedido do [CLIENTE %d] (prato %d).\n", id, pedido_fazer->id_cliente, pedido_fazer->id_prato);
        print_safe(buffer);

        // 3. Verifica estoque e prepara 
        pthread_mutex_lock(&mutex_estoque);

        // Se ele acordar e outro cozinheiro pegar o ingrediente, ele volta a dormir.
        // Se não tiver estoque:
        while (estoque[pedido_fazer->id_prato] == 0) {                          
            sprintf(buffer, "[▲ COZINHEIRO %d] Faltou igredientes para o prato %d. Acordando estoquista e esperando.\n", id, pedido_fazer->id_prato);
            print_safe(buffer);

            // 3a. Acorda o estoquista
            pthread_cond_signal(&cond_estoquista_precisa_repor);

            // 3b. Dorme e libera o mutex_estoque atomicamente
            pthread_cond_wait(&cond_estoque_reposto, &mutex_estoque);
        }

        // 3d. Se saiu do loop, é porque tem estoque
        estoque[pedido_fazer->id_prato]--;
        sprintf(buffer, "[▲ COZINHEIRO %d] Preparando prato %d... (Estoque agora: %d)\n", id, pedido_fazer->id_prato, estoque[pedido_fazer->id_prato]);
        print_safe(buffer);

        pthread_mutex_unlock(&mutex_estoque);

        sleep(2);

        // 4. Coloca na fila de pratos prontos
        pthread_mutex_lock(&mutex_fila_prontos);
        fila_pedidos_prontos[count_prontos++] = pedido_fazer;
        pthread_mutex_unlock(&mutex_fila_prontos);

        // 5. Acorda um garçom
        sem_post(&sem_pedidos_prontos);
    }

    sprintf(buffer, "[▲ COZINHEIRO %d] Encerrando turno.\n", id);
    print_safe(buffer);
    pthread_exit(NULL);
}

/*================================
========= ESTOQUISTA =============
================================*/

void* estoquista_func(void* arg) {
    int id = (intptr_t)arg;
    char buffer[200];
    sprintf(buffer, "[▩ ESTOQUISTA %d] Pronto para repor estoque.\n", id);
    print_safe(buffer);

    pthread_mutex_lock(&mutex_estoque);
    while(1) {
        // 1. Dorme até um cozinheiro acordá-lo
        pthread_cond_wait(&cond_estoquista_precisa_repor, &mutex_estoque);

        // 2. Verifica se acordou por causa do fechamento
        if (restaurante_fechado) {
            break; 
        }

        sprintf(buffer, "[▩ ESTOQUISTA %d] Fui acordado! Verificando estoques...\n", id);
        print_safe(buffer);
        int repos_algo = 0;

        // 3. Verifica TODOS os pratos (conforme especificação)
        for (int i = 0; i < TOTAL_PRATOS; i++) {
            if (estoque[i] <= 1) { // Repõe se for 0 ou 1 
                sprintf(buffer, "[▩ ESTOQUISTA %d] Reposto estoque do prato %d (era %d, agora e %d).\n", id, i, estoque[i], MAX_ESTOQUE_PRATO);
                print_safe(buffer);
                estoque[i] = MAX_ESTOQUE_PRATO;
                repos_algo = 1;
            }
        }

        // 4. Se repôs algo, acorda os cozinheiros
        if (repos_algo) {
            sprintf(buffer, "[▩ ESTOQUISTA %d] Reposicao concluida! Acordando cozinheiros.\n", id);
            print_safe(buffer);
            pthread_cond_broadcast(&cond_estoque_reposto);
        }
    }
    pthread_mutex_unlock(&mutex_estoque);

    sprintf(buffer, "[▩ ESTOQUISTA %d] Encerrando turno.\n", id);
    print_safe(buffer);
    pthread_exit(NULL);

}

/*================================
======== GESTOR DE MESAS =========
================================*/

void* gestor_mesas_func(void* arg) {
    char buffer[200];
    print_safe("[▬ GESTOR DE MESAS] Pronto para gerenciar mesas.\n");

    pthread_mutex_lock(&mutex_restaurante);
    while(1) {

        pthread_cond_wait(&cond_cliente_chegou, &mutex_restaurante);

        // 1. Condição de saída: Restaurante fechou E não tem ninguém esperando
        if (restaurante_fechado && clientes_esperando == 0) {
            break;  // Sai do loop infinito
        }

        // 2. Se acordou, é porque tem clientes esperando (ou o dia acabou)
        if (clientes_esperando > 0) {
            
            // Cenário 1: Tem mesa livre (outro cliente saiu)
            if (mesas_ocupadas < mesas_criadas) {
                // Não faz nada, só acorda os clientes
                sprintf(buffer, "[▬ GESTOR DE MESAS] Vi que tem vaga. Acordando clientes na fila!\n");
                print_safe(buffer);
                pthread_cond_broadcast(&cond_mesa_disponivel);
            }
            // Cenário 2: Sem mesa livre, mas pode adicionar uma nova
            else if (mesas_criadas < MAX_MESAS_RESTAURANTE) {
                mesas_criadas++;
                sprintf(buffer, "[▬ GESTOR DE MESAS] Adicionando mesa nova! (Total: %d)\n", mesas_criadas);
                print_safe(buffer);
                // Acorda os clientes para disputarem a nova mesa
                pthread_cond_broadcast(&cond_mesa_disponivel);
            }
            // Cenário 3: Restaurante lotado (sem mesas livres E sem poder adicionar)
            else {
                // Não faz nada, clientes terão que esperar
                // (O broadcast de 'mesa liberada' por outro cliente vai acordá-los)
            }
        }
    }

    pthread_mutex_unlock(&mutex_restaurante);
    print_safe("[▬ GESTOR DE MESAS] Encerrando turno.\n");
    pthread_exit(NULL);
}

/*================================
============ LIMPEZA =============
================================*/

void* responsavel_limpeza_func(void* arg) {
    char buffer[200];
    print_safe("[◈ LIMPEZA] Pronto para limpar. \n");

    while(1) {
        // 1. Espera ser acordado por uma mesa suja
        sem_wait(&sem_limpeza_necessaria);

        // 2. Verifica se foi acordado para fechar
        pthread_mutex_lock(&mutex_restaurante);
        if (restaurante_fechado && mesas_ocupadas == 0) {
            pthread_mutex_unlock(&mutex_restaurante);
            sem_post(&sem_limpeza_necessaria);
            break;
        }
        pthread_mutex_unlock(&mutex_restaurante);

        // 3. Limpa a mesa (simulação)
        sprintf(buffer, "[◈ LIMPEZA] Uma mesa esta suja. Limpando...\n");
        print_safe(buffer);
        sleep(2);

        // 4. Libera a mesa
        pthread_mutex_lock(&mutex_restaurante);
        mesas_ocupadas--;                                       // AGORA sim a mesa está livre
        sprintf(buffer, "[◈ LIMPEZA] Mesa limpa e liberada! (Vagas agora: %d)\n", mesas_criadas - mesas_ocupadas);
        print_safe(buffer);

        // Acorda clientes esperando e o gestor
        pthread_cond_signal(&cond_cliente_chegou);
        pthread_cond_broadcast(&cond_mesa_disponivel);

        // Incrementa e sinaliza
        clientes_que_sairam_total++;
        pthread_cond_broadcast(&cond_todos_clientes_sairam);

        // Verifica se é o último cliente
        if (restaurante_fechado && mesas_ocupadas == 0) {
            sprintf(buffer, "[◈ LIMPEZA] Limpei a ultima mesa! Avisando o gerente.\n");
            print_safe(buffer);
            pthread_cond_broadcast(&cond_todos_clientes_sairam);
        }

        pthread_mutex_unlock(&mutex_restaurante);
    }

    print_safe("[◈ LIMPEZA] Encerrando turno.\n");
    pthread_exit(NULL);
}

/*================================
========= TIMER DO DIA ===========
================================*/

// Esta thread é criada pelo Gerente e corre em paralelo.
// Seu único trabalho é dormir e "fechar" o restaurante no fim do tempo.

void* timer_restaurante_func(void* arg) {
    // Pega o tempo de duração do dia (em segundos)
    int segundos_dia = (intptr_t)arg; 
    char buffer[200];

    // 1. Dorme pelo tempo total do dia
    sleep(segundos_dia);

    // 2. O tempo acabou. Fecha o restaurante.
    pthread_mutex_lock(&mutex_restaurante);
    
    // Só fecha se o dia já não tiver terminado por outra razão
    if (!restaurante_fechado) {
        sprintf(buffer, "\n-------- TEMPO ESGOTADO! RESTAURANTE FECHANDO! --------\nEsperando os %d clientes saírem para finalizar expediente\n\n", mesas_ocupadas);
        print_safe(buffer);
        restaurante_fechado = 1;

        // Acorda todos os clientes na fila DE FORA (para irem embora)
        pthread_cond_broadcast(&cond_cliente_chegou);
        pthread_cond_broadcast(&cond_mesa_disponivel);

        // Acorda o GERENTE
        pthread_cond_broadcast(&cond_todos_clientes_sairam);
    }
    
    pthread_mutex_unlock(&mutex_restaurante);
    
    pthread_exit(NULL);
}