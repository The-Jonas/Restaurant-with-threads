#define _POSIX_C_SOURCE 200112L // Necessário para clock_gettime
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>                            // Para threads
#include <semaphore.h>                          // Para semáforos 
#include <unistd.h>                             // Para sleep
#include <time.h>                               // Para o timeout e o rand()
#include <errno.h>                              // Para ETIMEOUT


/*=======================================================*
 *=========== VARIÁVEIS GLOBAIS E DEFINIÇÕES ============*
 *=======================================================*/

// Inputs do usuário
int MAX_CLIENTES_POR_DIA;
int MAX_MESAS_RESTAURANTE;
int MAX_ESTOQUE_PRATO;

#define TOTAL_PRATOS 5;                                                // Total de pratos/receitas diferentes no restaurante
#define TIMEOUT_CLIENTE_ESPERA 5;                                      // Cliente espera 5 segundos max

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
pthread_cond_t cond_cliente_chegou;                                     // Cliente sinaliza pro gestor
pthread_cond_t cond_mesa_disponivel;                                    // Gestor sinaliza pro cliente

// -- Estado do Restaurante (reiniciado todo dia) ---
int mesas_criadas;                                                      // Começa com 0
int mesas_ocupadas;
int clientes_esperando;
int restaurante_fechado;                                                // 0 = ABERTO,  1 = FECHADO

/*=======================================================*
 *=========== STRUCTS (Estruturas de Dados) =============*
 *=======================================================*/


// Futuramente terá coisas aqui


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

// Função utilitária para imprimir com mutex
void print_safe(char* msg) {
    pthread_mutex_lock(&mutex_print);
    printf("%s", msg);
    pthread_mutex_unlock(&mutex_print);
}

 /*=======================================================*
  *======================== MAIN =========================*
  *=======================================================*/

int main() {
    // 1. Obter os inputs do usuário
    printf("Digite o número maximo de clientes por dia: ");
    scanf("%d", &MAX_CLIENTES_POR_DIA);

    printf("Digite o número máximo de mesas do restaurante: ");
    scanf("%d", &MAX_MESAS_RESTAURANTE);

    printf("Digite o estoque máximo de cada prato: ");
    scanf("%d", &MAX_ESTOQUE_PRATO);

    printf("\n---Iniciando simulação do Restaurante por uma semana---\n");

    pthread_mutex_init(&mutex_print, NULL);                                                   // Inicializar mutex de print
    
    // 2. Loop principal dos 7 dias
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
        
        // A primeira thread a ser chamada é gerente_do_dia
        pthread_t tif_gerente_dia;
        pthread_create(&tif_gerente_dia, NULL, gerente_do_dia_func, NULL);

        // A main espera o gerente do dia terminar
        // Ele só termina quando o dia acaba (restaurante fecha e todos os clientes saem)
        pthread_join(tif_gerente_dia, NULL);

        sprintf(buffer, "--- Fim do Dia %d ---\n", dia_atual);
        print_safe(buffer);
    }

    // 3. Fim da simulação
    printf("\n=================================================\n");
    printf("======== Simulacao de 7 dias encerrada. =========\n");
    printf("=================================================\n");

    // 4. Destruir mutexes e semaforos
    pthread_mutex_destroy(&mutex_print);
    pthread_mutex_destroy(&mutex_restaurante);
    pthread_cond_destroy(&cond_cliente_chegou);
    pthread_cond_destroy(&cond_mesa_disponivel);

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
    sprintf(buffer, "[GERENTE DO DIA] Bom dia! Iniciando o dia %d.\n", dia_atual);
    print_safe(buffer);

    // TODO: 
    // 1. Reinicializar variáveis do dia
    pthread_mutex_lock(&mutex_restaurante);
    mesas_criadas = 0;
    mesas_ocupadas = 0;
    clientes_esperando = 0;
    restaurante_fechado = 0;
    pthread_mutex_unlock(&mutex_restaurante);

    // 2. Acordar as threads "fixas"
    pthread_t tid_gestor_mesas;
    pthread_t tid_limpeza;
    //(Vamos criar garçons, cozinheiros, etc aqui...)
    pthread_create(&tid_gestor_mesas, NULL, gestor_mesas_func, NULL);
    pthread_create(&tid_limpeza, NULL, responsavel_limpeza_func, NULL);
    
    // 3. Loop para criar threads de clientes
    pthread_t clientes_threads[MAX_CLIENTES_POR_DIA];
    for (int i = 0; i < MAX_CLIENTES_POR_DIA; i++) {
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

        sleep(rand() % 4 + 2);                                                                      // Timer random de 2 a 5 segundos
    }

    // 4. Simular o fechamento do restaurante (timer)
    sleep(15);

    pthread_mutex_lock(&mutex_restaurante);
    print_safe("\n[GERENTE DO DIA] RESTAURANTE FECHANDO! Nao entram mais clientes.\n");
    restaurante_fechado = 1;
    // Acorda todos os threads esperando para que vejam que fechou
    pthread_cond_broadcast(&cond_cliente_chegou);
    pthread_cond_broadcast(&cond_mesa_disponivel);
    pthread_mutex_unlock(&mutex_restaurante);
    
    // 5. Esperar staff terminar
    // (O staff só vai terminar quando virem a flag 'restaurante_fechado'
    // E não tiver mais trabalho pendente)
    pthread_join(tid_gestor_mesas, NULL);
    pthread_join(tid_limpeza, NULL);
    // (Join dos outros staff aqui...)

    // 6. Esperar todos os clientes que *entraram* saírem
    // (O 'join' dos clientes é complexo, pois alguns podem
    // ter ido embora. Por enquanto, o 'join' do staff que depende
    // dos clientes é suficiente para o fim do dia)
    
    sprintf(buffer, "[GERENTE DO DIA] Encerrando o dia %d.\n", dia_atual);
    print_safe(buffer);

    // TODO: 
    // 1. Inicializar as threads "fixas" (2 Garçons, 2 Cozinheiros, 1 de cada dos outros) 
    // 2. Iniciar um loop para criar threads de clientes 
    //    - Usar um timer random (2 a 5 seg) para criar cada cliente 
    // 3. Gerenciar o timer de fechamento do restaurante 
    // 4. Esperar todos os clientes saírem antes de encerrar a thread
    
    pthread_exit(NULL);
}

/*================================
=========== CLIENTE ==============
================================*/

void* cliente_func(void* arg) {
    int id = *(int*)arg;
    free(arg);
    char buffer[200];

    sprintf(buffer, "[CLIENTE %d] Chegou ao restaurante.\n", id);
    print_safe(buffer);
    pthread_mutex_lock(&mutex_restaurante);

    pthread_mutex_lock(&mutex_restaurante);

    // 1. Verifica se o restaurante já tá fechado
    if (restaurante_fechado) {
        sprintf(buffer, "[CLIENTE %d] Restaurante ja esta fechado. Indo embora.\n", id);
        print_safe(buffer);
        pthread_mutex_unlock(&mutex_restaurante);
        pthread_exit(NULL);
    }

    // 2. Tenta pegar uma mesa
    if (mesas_ocupadas == mesas_criadas) {
        // 2a. Verifica se o restaurante está LOTADO (não pode colocar mais mesas)
        if (mesas_criadas == MAX_MESAS_RESTAURANTE) {
            sprintf(buffer, "[CLIENTE %d] Restaurante lotado. Vou esperar por uma vaga.\n", id);
            print_safe(buffer);
        } else {
            sprintf(buffer, "[CLIENTE %d] Sem mesas. Vou esperar adicionarem uma.\n", id);
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
            sprintf(buffer, "[CLIENTE %d] Cansei de esperar e FUI EMBORA.\n", id);
            print_safe(buffer);
            pthread_mutex_unlock(&mutex_restaurante);
            pthread_exit(NULL);
        }

        if (restaurante_fechado) {
            sprintf(buffer, "[CLIENTE %d] Restaurante fechou enquanto eu esperava. Indo embora.\n", id);
            print_safe(buffer);
            pthread_mutex_unlock(&mutex_restaurante);
            pthread_exit(NULL);
        }

        // Se saiu do loop e não deu timeout/fechou, é porque tem vaga!
        sprintf(buffer, "[CLIENTE %d] Consegui uma mesa apos esperar!\n", id);
        print_safe(buffer);

    } else {
        sprintf(buffer, "[CLIENTE %d] Sentei-me imediatamente.\n", id);
        print_safe(buffer);
    }

    // 3. Conseguiu a mesa (seja direto ou esperando)
    mesas_ocupadas++;
    pthread_mutex_unlock(&mutex_restaurante);

    // --- AQUI COMEÇA A PRÓXIMA ETAPA: FAZER PEDIDO ---
    // (simulando o tempo no restaurante)
    sprintf(buffer, "[CLIENTE %d] Estou comendo/no restaurante...\n", id);
    print_safe(buffer);
    sleep(rand() % 5 + 3); // Simula tempo comendo/pedindo


    // 4. Sair do restaurante
    pthread_mutex_lock(&mutex_restaurante);
    mesas_ocupadas--;
    sprintf(buffer, "[CLIENTE %d] Terminei. Liberando a mesa. (Vagas agora: %d)\n", id, mesas_criadas - mesas_ocupadas);
    print_safe(buffer);
    
    // TODO: 50% chance de sujar [cite: 49]
    
    // Acorda o gestor (para o caso de ter sido a última mesa)
    // E acorda clientes esperando (pois uma mesa vagou)
    pthread_cond_signal(&cond_cliente_chegou);
    pthread_cond_broadcast(&cond_mesa_disponivel);
    
    pthread_mutex_unlock(&mutex_restaurante);

    // TODO: 
    // 1. Tentar pegar uma mesa (interagir com gestor_mesas) 
    // 2. Se não conseguir, esperar. Se demorar, ir embora 
    // 3. Se conseguir, fazer pedido (interagir com garçom) 
    // 4. Esperar pedido, comer 
    // 5. Pagar (interagir com garçom) 
    // 6. 50% chance de sujar a mesa 
    // 7. Liberar a mesa e ir embora 
    pthread_exit(NULL);
}

/*================================
============= GARÇOM =============
================================*/

void* garcom_func(void* arg) {
    // TODO:
    // 1. Esperar por um cliente que queira pedir 
    // 2. Anotar pedido e entregar ao cozinheiro 
    // 3. Esperar prato ficar pronto 
    // 4. Entregar prato ao cliente 
    // 5. Receber pagamento 
    // 6. Se mesa suja, acordar limpeza 
    pthread_exit(NULL);
}

/*================================
========== COZINHEIRO ============
================================*/

void* cozinheiro_func(void* arg) {
    // TODO:
    // 1. Esperar pedido do garçom 
    // 2. Verificar estoque 
    // 3. Se não tiver, acordar estoquista 
    // 4. Se tiver, preparar (sleep) e colocar na fila de entrega 
    // 5. Só faz um pedido de cada vez 
    pthread_exit(NULL);
}

/*================================
========= ESTOQUISTA =============
================================*/

void* estoquista_func(void* arg) {
    // TODO:
    // 1. Ficar "dormindo" (esperando em um semáforo/condicional)
    // 2. Ser acordado pelo cozinheiro 
    // 3. Repor estoque do prato que acabou 
    // 4. Verificar outros pratos e repor se estoque = 1 
    // 5. Voltar a "dormir"
    pthread_exit(NULL);
}

/*================================
======== GESTOR DE MESAS =========
================================*/

void* gestor_mesas_func(void* arg) {
    char buffer[200];
    print_safe("[GESTOR DE MESAS] Pronto para gerenciar mesas.\n");

    pthread_mutex_lock(&mutex_restaurante);
    while(1) {
        // 1. Dorme até um cliente sinalizar
        while (clientes_esperando == 0 && !restaurante_fechado) {
            pthread_cond_wait(&cond_cliente_chegou, &mutex_restaurante);
        }

        // 2. Condição de saída: Restaurante fechou E não tem ninguém esperando
        if (restaurante_fechado && clientes_esperando == 0) {
            break;  // Sai do loop infinito
        }

        // 3. Se acordou, é porque tem clientes esperando (ou o dia acabou)
        if (clientes_esperando > 0) {
            
            // Cenário 1: Tem mesa livre (outro cliente saiu)
            if (mesas_ocupadas < mesas_criadas) {
                // Não faz nada, só acorda os clientes
                sprintf(buffer, "[GESTOR DE MESAS] Vi que tem vaga. Acordando clientes na fila!\n");
                print_safe(buffer);
                pthread_cond_broadcast(&cond_mesa_disponivel);
            }
            // Cenário 2: Sem mesa livre, mas pode adicionar uma nova
            else if (mesas_criadas < MAX_MESAS_RESTAURANTE) {
                mesas_criadas++;
                sprintf(buffer, "[GESTOR DE MESAS] Adicionando mesa nova! (Total: %d)\n", mesas_criadas);
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

    print_safe("[GESTOR DE MESAS] Encerrando turno.\n");
    pthread_exit(NULL);
}

/*================================
============ LIMPEZA =============
================================*/

void* responsavel_limpeza_func(void* arg) {
    // Por enquanto, só fica ativo e encerra quando o dia acaba
    pthread_mutex_lock(&mutex_restaurante);
    while(!restaurante_fechado) {
        // (Lógica de esperar ser acordado virá aqui)
        pthread_mutex_unlock(&mutex_restaurante);
        sleep(1);
        pthread_mutex_lock(&mutex_restaurante);
    }
    pthread_mutex_unlock(&mutex_restaurante);
    print_safe("[LIMPEZA] Encerrando turno.\n");
    // TODO:
    // 1. Ficar "dormindo"
    // 2. Ser acordado pelo garçom 
    // 3. Limpar a mesa (sleep)
    // 4. Sinalizar que a mesa está livre
    // 5. Limpar mesas restantes no fim do dia 
    pthread_exit(NULL);
}