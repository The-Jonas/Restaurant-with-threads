/*
 * Arquivo: restaurante.c
 * Descrição: Simulação de um restaurante concorrente com Múltiplas Threads.
 *
 * Compilação:
 * gcc restaurante.c -o restaurante -lpthread
 *
 * (Adicionado -lpthread para linkar a biblioteca POSIX Threads)
 *
 * NOTA: A macro _POSIX_C_SOURCE é necessária para expor
 * as funções clock_gettime e pthread_cond_timedwait.
 */
#define _POSIX_C_SOURCE 200112L // Necessário para clock_gettime

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h> // Para checar o ETIMEDOUT

// -- DEFINIÇÕES GLOBAIS -- \\

#define MAX_PRATOS 5
#define MAX_IGREDIENTES 10
#define NUM_COZINHEIROS 3
#define NUM_GARCONS 2
#define NUM_LIMPEZA 1

// -- ESTRUTURAS DE DADOS -- \\ 

//Estrutura para representar um pedido
typedef struct {
    int id_cliente;
    int tipo_prato;
    int mesa_id; // Adicionado para o garçom saber onde entregar
} Pedido;

//Estrutura para representar uma mesa
typedef enum {
    LIVRE,
    OCUPADA,
    SUJA
} StatusMesa;

typedef struct {
    int id_mesa;
    StatusMesa status;
} Mesa;

//Estrutura para representar os igredientes de um prato
typedef struct {
    int igredientes[MAX_IGREDIENTES]; // Array dos IDs dos ingredientes necessários
    int quantidade_igredientes;
} Receita;

// -- VARIÁVEIS GLOBAIS COMPARTILHADAS -- \\

//Sincronização
pthread_mutex_t mutex_mesas;
pthread_mutex_t mutex_pedidos;
pthread_mutex_t mutex_pratos_prontos;
pthread_mutex_t mutex_estoque;
pthread_mutex_t mutex_restaurante;
pthread_mutex_t mutex_pagamentos;           // Para a logica de pagamento
pthread_mutex_t mutex_printf;               // NOVO: Para evitar printf misturado

pthread_cond_t cond_mesa_livre;
pthread_cond_t cond_pedido_feito;            // Sinaliza Cozinheiro que há pedido
pthread_cond_t cond_prato_pronto;            // Sinaliza Garçom que há prato pronto
pthread_cond_t cond_estoque_disponivel;      // Sinaliza Cozinheiro (estoque reposto) ou Estoquista (estoque baixo)
pthread_cond_t cond_mesa_suja;
pthread_cond_t cond_demanda_por_mesas;       // Sinaliza Gestor de Mesas
pthread_cond_t cond_garcom_pedido_pronto;    // Sinaliza Cliente que o prato chegou
pthread_cond_t cond_pagamento;               // Sinaliza Garçom que cliente quer pagar

//Estado do Restaurante
int num_clientes_max;
int num_mesas_max;
int mesas_ocupadas = 0;
int mesas_sujas = 0;
int num_pratos = MAX_PRATOS;

Mesa* mesas;

//Fila de pedidos
Pedido* fila_pedidos;
int pedidos_count = 0;

//Fila de pratos prontos
Pedido* fila_pratos_prontos;
int pratos_prontos_count = 0;

//Estoque e Receitas
int estoque[MAX_IGREDIENTES];
Receita receitas[MAX_PRATOS];

//Gerente do Dia
typedef enum {
    ABERTO,
    FECHADO,
    FIM_DE_SEMANA
} StatusRestaurante;

StatusRestaurante estado_restaurante = ABERTO;
int dia_da_semana = 1;

int mesas_ativas = 0;             // Variável para rastrear o número de mesas ativas
int clientes_esperando = 0;       // Variável para rastrear o número de clientes que não conseguiram sentar
int ingredientes_em_falta = 0;    // Quantidade de tipos de ingredientes em falta
pthread_t thread_clientes[200];   // Array para as threads de clientes (aumentado para segurança)
int clientes_criados = 0;


// -- ASSINATURA DAS FUNÇÕES DAS ENTIDADES -- \\

void *cliente(void *arg);
void *cozinheiro(void *arg);
void *garcom(void *arg);
void *responsavel_limpeza(void *arg);
void *estoquista(void *arg);
void *gestor_mesas(void *arg);
void *gerente_dia(void *arg);

// -- MAIN -- \\

int main(int argc, char *argv[]) {
    // 1. Receber inputs do usuário
    printf("Digite a capacidade máxima de clientes: ");
    scanf("%d", &num_clientes_max);
    printf("Digite a capacidade maxima de mesas: ");
    scanf("%d", &num_mesas_max);

    // CORREÇÃO: Inicializa o gerador de números aleatórios
    srand(time(NULL));

    // 2. Inicializar recursos
    pthread_mutex_init(&mutex_mesas, NULL);
    pthread_mutex_init(&mutex_pedidos, NULL);
    pthread_mutex_init(&mutex_pratos_prontos, NULL);
    pthread_mutex_init(&mutex_estoque, NULL);
    pthread_mutex_init(&mutex_restaurante, NULL);
    pthread_mutex_init(&mutex_pagamentos, NULL);
    pthread_mutex_init(&mutex_printf, NULL); // NOVO: Inicializa mutex de printf

    pthread_cond_init(&cond_mesa_livre, NULL);
    pthread_cond_init(&cond_pedido_feito, NULL);
    pthread_cond_init(&cond_prato_pronto, NULL);
    pthread_cond_init(&cond_estoque_disponivel, NULL);
    pthread_cond_init(&cond_mesa_suja, NULL);
    pthread_cond_init(&cond_demanda_por_mesas, NULL);
    pthread_cond_init(&cond_garcom_pedido_pronto, NULL); 
    pthread_cond_init(&cond_pagamento, NULL);          

    //Alocar memória para as estruturas
    mesas = (Mesa*) malloc(num_mesas_max * sizeof(Mesa));
    fila_pedidos = (Pedido*) malloc(num_clientes_max *sizeof(Pedido));
    fila_pratos_prontos = (Pedido*) malloc(num_clientes_max * sizeof(Pedido));

    // Inicializar estoque e receitas (Exemplo simples)
    for(int i = 0; i < MAX_IGREDIENTES; i++) {
        estoque[i] = 10; // Começa com 10 de cada ingrediente
    }
    for(int i = 0; i < MAX_PRATOS; i++) {
        receitas[i].igredientes[0] = i % MAX_IGREDIENTES; // Prato 'i' usa ingrediente 'i'
        receitas[i].quantidade_igredientes = 1;
    }


    // 3. Criar as threads
    pthread_t thread_cozinheiros[NUM_COZINHEIROS];
    pthread_t thread_garcons[NUM_GARCONS];
    pthread_t thread_limpeza[NUM_LIMPEZA];
    pthread_t thread_estoquista;
    pthread_t thread_gestor_mesas;
    pthread_t thread_gerente_dia;
    // thread_clientes é global

    //Criar threads para entidades que existem continuamente
    for (int i = 0; i < NUM_COZINHEIROS; i++) {
        // CORREÇÃO: Passando o ID do cozinheiro
        int *id = (int *)malloc(sizeof(int));
        *id = i;
        pthread_create(&thread_cozinheiros[i], NULL, cozinheiro, (void *)id);
    }

    for (int i = 0; i < NUM_GARCONS; i++) {
        // CORREÇÃO: Passando o ID do garçom
        int *id = (int *)malloc(sizeof(int));
        *id = i;
        pthread_create(&thread_garcons[i], NULL, garcom, (void *)id);
    }

    for (int i = 0; i < NUM_LIMPEZA; i++) {
        pthread_create(&thread_limpeza[i], NULL, responsavel_limpeza, NULL);
    }

    pthread_create(&thread_estoquista, NULL, estoquista, NULL);
    pthread_create(&thread_gestor_mesas, NULL, gestor_mesas, NULL);
    pthread_create(&thread_gerente_dia, NULL, gerente_dia, NULL);

    // Espera o gerente do dia terminar (o que sinaliza o fim da simulação)
    pthread_join(thread_gerente_dia, NULL);
    
    // TODO: Destruir mutexes e conds, e dar free nas memórias alocadas.
    return 0;
}

// -- IMPLEMENTAÇÃO DAS FUNÇÕES DAS ENTIDADES -- \\

void *cliente(void *arg) {
    int id_cliente = *((int *)arg);
    free(arg); // Libera memória alocada para o id no gerente_dia

    struct timespec tempo_limite;
    int mesa_alocada = -1;

    // 1. Cliente tenta conseguir uma mesa
    pthread_mutex_lock(&mutex_mesas);

    // CORREÇÃO: Cliente espera se mesas ocupadas >= mesas ATIVAS
    while (mesas_ocupadas >= mesas_ativas) {
        pthread_mutex_lock(&mutex_printf);
        printf("Cliente %d: Não tem mesas disponivéis, vou esperar na fila! (Ocupadas: %d, Ativas: %d)\n", id_cliente, mesas_ocupadas, mesas_ativas);
        pthread_mutex_unlock(&mutex_printf);
        
        clientes_esperando++;
        pthread_cond_signal(&cond_demanda_por_mesas);

        //Define um tempo limite de 5 segundos para a espera
        clock_gettime(CLOCK_REALTIME, &tempo_limite);
        tempo_limite.tv_sec += 5;

        int status = pthread_cond_timedwait(&cond_mesa_livre, &mutex_mesas, &tempo_limite);

        //Se o tempo esgotar, o cliente desiste e vai embora
        if (status == ETIMEDOUT) {
            pthread_mutex_lock(&mutex_printf);
            printf("Cliente %d: Sem paciência! Foi embora porque não tinha mesas.\n", id_cliente);
            pthread_mutex_unlock(&mutex_printf);
            clientes_esperando--; // Sai da fila de espera
            pthread_mutex_unlock(&mutex_mesas);
            return NULL;
        }
        // Se foi acordado, o loop 'while' re-checa a condição
    }

    //Se o cliente chegou até aqui, uma mesa está disponivel. Ele a ocupa
    // CORREÇÃO: Procura apenas nas mesas ATIVAS
    for (int i = 0; i < mesas_ativas; i++) {
        if (mesas[i].status == LIVRE) {
            mesas[i].status = OCUPADA; // CORREÇÃO: '=' é atribuição
            mesa_alocada = i;
            mesas_ocupadas++;
            
            // CORREÇÃO: Se ele pegou uma mesa, ele não está mais "esperando"
            // (Esta checagem é uma segurança)
            if(clientes_esperando > 0) {
                clientes_esperando--;
            }

            pthread_mutex_lock(&mutex_printf);
            printf("Cliente %d: Consegui uma mesa. Mesa %d ocupada. Total de mesas ocupadas: %d.\n", id_cliente, mesa_alocada, mesas_ocupadas);
            pthread_mutex_unlock(&mutex_printf);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_mesas);

    // 2. Cliente faz o pedido
    pthread_mutex_lock(&mutex_pedidos);
    Pedido pedido_do_cliente;
    pedido_do_cliente.id_cliente = id_cliente;
    pedido_do_cliente.tipo_prato = rand() % MAX_PRATOS;
    pedido_do_cliente.mesa_id = mesa_alocada;
    fila_pedidos[pedidos_count] = pedido_do_cliente;
    pedidos_count++;
    pthread_mutex_lock(&mutex_printf);
    printf("Cliente %d: Fiz um pedido (Prato %d). Total de pedidos na fila: %d.\n", id_cliente, pedido_do_cliente.tipo_prato, pedidos_count);
    pthread_mutex_unlock(&mutex_printf);
    // Sinaliza ao Cozinheiro que há um pedido
    pthread_cond_signal(&cond_pedido_feito); 
    pthread_mutex_unlock(&mutex_pedidos);

    // 3. Cliente espera o prato
    // Usa mutex_pagamentos para sincronizar a entrega e o pagamento com o garçom
    pthread_mutex_lock(&mutex_pagamentos); 
    pthread_mutex_lock(&mutex_printf);
    printf("Cliente %d: Esperando meu prato...\n", id_cliente);
    pthread_mutex_unlock(&mutex_printf);
    // Espera o garçom sinalizar que o prato foi entregue
    pthread_cond_wait(&cond_garcom_pedido_pronto, &mutex_pagamentos); 
    pthread_mutex_lock(&mutex_printf);
    printf("Cliente %d: O prato chegou! Comendo...\n", id_cliente);
    pthread_mutex_unlock(&mutex_printf);
    pthread_mutex_unlock(&mutex_pagamentos);
    
    sleep(rand() % 5 + 3); // Tempo comendo

    // 4. Cliente terminou de comer e sinaliza para o garçom pagar
    pthread_mutex_lock(&mutex_pagamentos);
    pthread_mutex_lock(&mutex_printf);
    printf("Cliente %d: Terminei de comer, esperando o garçom para pagar.\n", id_cliente);
    pthread_mutex_unlock(&mutex_printf);
    pthread_cond_signal(&cond_pagamento); // Sinaliza que está pronto para pagar
    
    // 5. Cliente espera o garçom pegar o pagamento e o acordar
    pthread_cond_wait(&cond_pagamento, &mutex_pagamentos); 
    pthread_mutex_lock(&mutex_printf);
    printf("Cliente %d: Paguei a conta. Indo embora!\n", id_cliente);
    pthread_mutex_unlock(&mutex_printf);
    pthread_mutex_unlock(&mutex_pagamentos);

    // 6. Cliente sinaliza que está saindo e libera a mesa
    pthread_mutex_lock(&mutex_mesas);
    mesas_ocupadas--;
    int mesa_suja = rand() % 2; 

    if (mesa_suja) {
        mesas[mesa_alocada].status = SUJA;
        mesas_sujas++;
        pthread_mutex_lock(&mutex_printf);
        printf("Cliente %d: Saiu. Mesa %d agora esta suja. Mesas ocupadas: %d.\n", id_cliente, mesa_alocada, mesas_ocupadas);
        pthread_mutex_unlock(&mutex_printf);
        pthread_cond_signal(&cond_mesa_suja); // Sinaliza para a limpeza
    } else {
        mesas[mesa_alocada].status = LIVRE;
        pthread_mutex_lock(&mutex_printf);
        printf("Cliente %d: Saiu. Mesa %d agora esta livre (e ainda limpa). Mesas ocupadas: %d.\n", id_cliente, mesa_alocada, mesas_ocupadas);
        pthread_mutex_unlock(&mutex_printf);
        // CORREÇÃO: Sinaliza ao gestor que uma mesa ficou livre
        pthread_cond_signal(&cond_demanda_por_mesas); 
    }
    pthread_mutex_unlock(&mutex_mesas);

    return NULL;
}

void *cozinheiro(void *arg) {
    int id_cozinheiro = *((int *)arg);
    free(arg);
    
    while (estado_restaurante != FECHADO) { // Continua trabalhando até o restaurante fechar
        pthread_mutex_lock(&mutex_pedidos);
    
        //O cozinheiro espera que haja pedidos na fila
        while (pedidos_count == 0 && estado_restaurante == ABERTO) {
            pthread_mutex_lock(&mutex_printf);
            printf("Cozinheiro %d: Não há pedidos, esperando por trabalho...\n", id_cozinheiro);
            pthread_mutex_unlock(&mutex_printf);
            pthread_cond_wait(&cond_pedido_feito, &mutex_pedidos);
        }

        // Se o restaurante fechou e não há mais pedidos, o cozinheiro vai embora
        if (estado_restaurante == FECHADO && pedidos_count == 0) {
            pthread_mutex_unlock(&mutex_pedidos);
            break; // Sai do loop while
        }

        // 1. Pega um pedido da fila
        Pedido pedido_atual = fila_pedidos[0];
        for (int i = 0; i < pedidos_count - 1; i++) {
            fila_pedidos[i] = fila_pedidos[i+1];
        }
        pedidos_count --;

        pthread_mutex_lock(&mutex_printf);
        printf("Cozinheiro %d: Peguei o pedido do cliente %d. Pedidos na fila: %d.\n", id_cozinheiro, pedido_atual.id_cliente, pedidos_count);
        pthread_mutex_unlock(&mutex_printf);
        pthread_mutex_unlock(&mutex_pedidos);

        // 2. Lógica de Ingredientes
        int ingrediente_necessario = receitas[pedido_atual.tipo_prato].igredientes[0];
        pthread_mutex_lock(&mutex_estoque);
        
        // Espera se o ingrediente necessário está em falta
        while (estoque[ingrediente_necessario] <= 0) {
            pthread_mutex_lock(&mutex_printf);
            printf("Cozinheiro %d: Ingrediente %d em falta para o prato %d. Esperando estoquista...\n", id_cozinheiro, ingrediente_necessario, pedido_atual.tipo_prato);
            pthread_mutex_unlock(&mutex_printf);
            
            ingredientes_em_falta++; // Informa ao estoquista
            pthread_cond_signal(&cond_estoque_disponivel); // Acorda o estoquista
            
            // Espera o estoquista repor
            pthread_cond_wait(&cond_estoque_disponivel, &mutex_estoque);
        }

        // 3. Retira o ingrediente
        estoque[ingrediente_necessario]--;
        pthread_mutex_lock(&mutex_printf);
        printf("Cozinheiro %d: Ingrediente %d retirado. Estoque restante: %d\n", id_cozinheiro, ingrediente_necessario, estoque[ingrediente_necessario]);
        pthread_mutex_unlock(&mutex_printf);
        pthread_mutex_unlock(&mutex_estoque);


        // 4. Prepara o prato
        pthread_mutex_lock(&mutex_printf);
        // CORREÇÃO: Imprimindo o tipo do prato
        printf("Cozinheiro %d: Preparando o prato %d para o cliente %d...\n", id_cozinheiro, pedido_atual.tipo_prato, pedido_atual.id_cliente);
        pthread_mutex_unlock(&mutex_printf);
        sleep(4); // Simula tempo de preparo

        // 5. Coloca o prato na fila de pratos prontos
        pthread_mutex_lock(&mutex_pratos_prontos);
        fila_pratos_prontos[pratos_prontos_count] = pedido_atual;
        pratos_prontos_count++;
        pthread_mutex_lock(&mutex_printf);
        printf("Cozinheiro %d: Prato do cliente %d pronto. Pratos na fila: %d.\n", id_cozinheiro, pedido_atual.id_cliente, pratos_prontos_count);
        pthread_mutex_unlock(&mutex_printf);

        // 6. Sinaliza para o garçom que há um prato pronto
        // Broadcast é mais seguro se houver múltiplos garçons
        pthread_cond_broadcast(&cond_prato_pronto); 
        pthread_mutex_unlock(&mutex_pratos_prontos);
    }
    
    pthread_mutex_lock(&mutex_printf);
    printf("Cozinheiro %d: Encerrando operação...\n", id_cozinheiro);
    pthread_mutex_unlock(&mutex_printf);
    return NULL;
}

void *garcom(void *arg) {
    int id_garcom = *((int *)arg);
    free(arg); 

    while (estado_restaurante != FECHADO){

        // O garçom tem duas tarefas principais: entregar pratos e receber pagamentos.
        // Vamos usar um mutex 'global' para o garçom decidir o que fazer.
        // Esta é uma lógica simplificada; uma implementação mais complexa
        // usaria 'select' ou 'pthread_trylock' para checar múltiplas filas.

        // Por enquanto, o garçom prioriza a entrega de pratos.
        pthread_mutex_lock(&mutex_pratos_prontos);

        // 1. Entregar pratos prontos
        // CORREÇÃO: '==' para comparação
        while (pratos_prontos_count == 0 && estado_restaurante == ABERTO) { 
            pthread_mutex_lock(&mutex_printf);
            printf("Garçom %d: Não há pratos prontos, esperando na cozinha...\n", id_garcom);
            pthread_mutex_unlock(&mutex_printf);
            pthread_cond_wait(&cond_prato_pronto, &mutex_pratos_prontos);
        }

        // Se o restaurante fechou e não há mais pratos, o garçom vai embora
        if (estado_restaurante == FECHADO && pratos_prontos_count == 0) {
            pthread_mutex_unlock(&mutex_pratos_prontos);
            break; // Sai do loop while
        }

        Pedido prato_pronto = fila_pratos_prontos[0];
        for (int i = 0; i < pratos_prontos_count - 1; i++) {
            fila_pratos_prontos[i] = fila_pratos_prontos[i+1];
        }
        pratos_prontos_count--;

        pthread_mutex_lock(&mutex_printf);
        printf("Garçom %d: Peguei o prato do cliente %d. Pratos na fila: %d.\n", id_garcom, prato_pronto.id_cliente, pratos_prontos_count);
        pthread_mutex_unlock(&mutex_printf);
        pthread_mutex_unlock(&mutex_pratos_prontos);
        
        pthread_mutex_lock(&mutex_printf);
        printf("Garçom %d: Entregando prato para o cliente %d...\n", id_garcom, prato_pronto.id_cliente);
        pthread_mutex_unlock(&mutex_printf);
        sleep(2);

        // 2. Acorda o cliente que está esperando pelo prato
        pthread_mutex_lock(&mutex_pagamentos); 
        pthread_cond_signal(&cond_garcom_pedido_pronto);
        pthread_mutex_unlock(&mutex_pagamentos);

        // 3. Receber pagamento e liberar a mesa
        pthread_mutex_lock(&mutex_pagamentos); 
        pthread_mutex_lock(&mutex_printf);
        printf("Garçom %d: Esperando cliente %d terminar para pagar.\n", id_garcom, prato_pronto.id_cliente);
        pthread_mutex_unlock(&mutex_printf);
        
        // Espera o cliente sinalizar que terminou de comer
        pthread_cond_wait(&cond_pagamento, &mutex_pagamentos);
        
        pthread_mutex_lock(&mutex_printf);
        printf("Garçom %d: Cliente %d terminou de comer. Recebendo pagamento...\n", id_garcom, prato_pronto.id_cliente);
        pthread_mutex_unlock(&mutex_printf);
        sleep(1); // Simula o processamento do pagamento
        
        // Acorda o cliente para que ele possa ir embora
        pthread_cond_signal(&cond_pagamento);
        pthread_mutex_unlock(&mutex_pagamentos);
    }

    pthread_mutex_lock(&mutex_printf);
    printf("Garçom %d: Encerrando operação...\n", id_garcom);
    pthread_mutex_unlock(&mutex_printf);
    return NULL;
}

void *responsavel_limpeza(void *arg) {
    while (estado_restaurante != FECHADO) {
        pthread_mutex_lock(&mutex_mesas);

        //A thread da limpeza espera que haja pelo menos uma mesa suja
        while (mesas_sujas == 0 && estado_restaurante == ABERTO) {
            pthread_mutex_lock(&mutex_printf);
            printf("Limpeza: Nenhuma mesa suja. Esperando...\n");
            pthread_mutex_unlock(&mutex_printf);
            pthread_cond_wait(&cond_mesa_suja, &mutex_mesas);
        }

        // Se o restaurante fechou e não há mais mesas sujas, vai embora
        if (estado_restaurante == FECHADO && mesas_sujas == 0) {
            pthread_mutex_unlock(&mutex_mesas);
            break;
        }

        //Procura uma mesa suja para limpar
        for (int i = 0; i < mesas_ativas; i++) { // CORREÇÃO: Itera até mesas_ativas
            if (mesas[i].status == SUJA) {
                pthread_mutex_lock(&mutex_printf);
                printf("Limpeza: Começando a limpar a mesa %d.\n", i);
                pthread_mutex_unlock(&mutex_printf);
                sleep(2);

                //A mesa agora é livre e limpa
                mesas[i].status = LIVRE;
                mesas_sujas--;
                pthread_mutex_lock(&mutex_printf);
                printf("Limpeza: Mesa %d limpa. Mesas sujas restantes: %d.\n", i, mesas_sujas);
                pthread_mutex_unlock(&mutex_printf);

                //Sinaliza para o gestor de mesas que uma vaga foi liberada
                pthread_cond_signal(&cond_demanda_por_mesas);

                break;
            }
        }
        pthread_mutex_unlock(&mutex_mesas);
    }
    pthread_mutex_lock(&mutex_printf);
    printf("Limpeza: Encerrando operação...\n");
    pthread_mutex_unlock(&mutex_printf);
    return NULL;
}

void *estoquista(void *arg) {   
    while (estado_restaurante != FECHADO) {
        pthread_mutex_lock(&mutex_estoque);

        //O estoquista espera até que algum tipo de ingrediente esteja em falta
        while (ingredientes_em_falta == 0 && estado_restaurante == ABERTO) {
            pthread_mutex_lock(&mutex_printf);
            printf("Estoquista: Estoque OK, sem trabalho. Esperando...\n");
            pthread_mutex_unlock(&mutex_printf);
            pthread_cond_wait(&cond_estoque_disponivel, &mutex_estoque);
        }
        
        // Se o restaurante fechou, vai embora
        if (estado_restaurante == FECHADO) {
            pthread_mutex_unlock(&mutex_estoque);
            break;
        }

        //Se chegou aqui, é porque há ingredientes em falta. Ele vai repor.
        pthread_mutex_lock(&mutex_printf);
        printf("Estoquista: Recebi um aviso! vou verificar o estoque e repor o que for necessário.\n");
        pthread_mutex_unlock(&mutex_printf);

        //Lógica de reposição de ingredientes
        for (int i = 0; i < MAX_IGREDIENTES; i++) {
            // Se o nível de um ingrediente estiver abaixo ou igual a 2, ele repõe
            if (estoque[i] <= 2) {
                estoque[i] = 10; // Valor de reposição
                pthread_mutex_lock(&mutex_printf);
                printf("Estoquista: Repus o ingrediente %d. Nivel agora: %d.\n", i, estoque[i]); 
                pthread_mutex_unlock(&mutex_printf);
            }
        }

        // Zera a contagem de ingredientes em falta após repor
        ingredientes_em_falta = 0;
        
        // Avisa os cozinheiros que ingredientes podem estar disponiveis agora
        pthread_cond_broadcast(&cond_estoque_disponivel);

        pthread_mutex_unlock(&mutex_estoque);
    }

    pthread_mutex_lock(&mutex_printf);
    printf("Estoquista: Encerrando operation...\n");
    pthread_mutex_unlock(&mutex_printf);
    return NULL;
}

void *gestor_mesas(void *arg) {
    while (estado_restaurante != FECHADO) {
        pthread_mutex_lock(&mutex_mesas);

        // CORREÇÃO: Lógica de espera simplificada.
        // O gestor espera se não há clientes na fila E todas as mesas ativas estão ocupadas.
        while (clientes_esperando == 0 && mesas_ocupadas >= mesas_ativas && estado_restaurante == ABERTO) {
            pthread_mutex_lock(&mutex_printf);
            printf("Gestor de Mesas: Sem demanda e mesas cheias. Esperando...\n");
            pthread_mutex_unlock(&mutex_printf);
            pthread_cond_wait(&cond_demanda_por_mesas, &mutex_mesas); 
        }

        // Se o restaurante fechou, vai embora
        if (estado_restaurante == FECHADO) {
            pthread_mutex_unlock(&mutex_mesas);
            break;
        }

        // Se foi acordado, checa o que precisa ser feito:

        // Cenário 1: Tem cliente esperando E tem mesa livre (limpa ou educada)
        if (clientes_esperando > 0 && mesas_ocupadas < mesas_ativas) {
            pthread_mutex_lock(&mutex_printf);
            printf("Gestor de Mesas: Demanda detectada, mesa livre já existe. Acordando clientes.\n");
            pthread_mutex_unlock(&mutex_printf);
            pthread_cond_broadcast(&cond_mesa_livre); // Acorda clientes para disputar a vaga
        
        // Cenário 2: Tem cliente esperando E NÃO tem mesa livre, MAS pode adicionar
        } else if (clientes_esperando > 0 && mesas_ativas < num_mesas_max) {
            mesas_ativas++;
            mesas[mesas_ativas - 1].id_mesa = mesas_ativas - 1;
            mesas[mesas_ativas - 1].status = LIVRE;
            
            pthread_mutex_lock(&mutex_printf);
            printf("Gestor de Mesas: Demanda detectada, SEM mesas livres. Adicionando nova mesa %d. Total: %d.\n", mesas_ativas - 1, mesas_ativas);
            pthread_mutex_unlock(&mutex_printf);
            
            pthread_cond_broadcast(&cond_mesa_livre); // Acorda clientes para disputar a nova vaga
        
        // Cenário 3: Tem cliente esperando, MAS restaurante está LOTADO
        } else if (clientes_esperando > 0) {
             pthread_mutex_lock(&mutex_printf);
             printf("Gestor de Mesas: Demanda detectada, mas estamos LOTADOS. Clientes terão que esperar timeout. (Ocupadas: %d, Ativas: %d)\n", mesas_ocupadas, mesas_ativas);
             pthread_mutex_unlock(&mutex_printf);
        }

        pthread_mutex_unlock(&mutex_mesas);
        
        sleep(1); // Delay do gestor
    }

    pthread_mutex_lock(&mutex_printf);
    printf("Gestor de Mesas: Encerrando operação...\n");
    pthread_mutex_unlock(&mutex_printf);
    return NULL;
}

void *gerente_dia(void *arg) {
    // A semente do rand() já foi inicializada no main

    while (dia_da_semana <= 7) {
        pthread_mutex_lock(&mutex_restaurante);
        estado_restaurante = ABERTO;
        pthread_mutex_lock(&mutex_printf);
        printf("\nGerente: Novo dia. Hoje e o dia %d. Restaurante ABERTO!\n", dia_da_semana);
        pthread_mutex_unlock(&mutex_printf);
        pthread_mutex_unlock(&mutex_restaurante);

        //Simula o tempo de abertura do restaurante durante o dia
        int tempo_aberto = 0;
        // O loop agora cria clientes E verifica o estado do restaurante
        while (tempo_aberto < 25 && clientes_criados < num_clientes_max) { 
            //Simula a chegada de clientes em intervalos aleátorios (de 1 a 3 segundos)
            sleep(rand() % 3 + 1);

            //Cria a thread do cliente
            int *id_cliente = (int *)malloc(sizeof(int));
            *id_cliente = clientes_criados;

            pthread_create(&thread_clientes[clientes_criados], NULL, cliente, (void *)id_cliente);
            pthread_mutex_lock(&mutex_printf);
            printf("Gerente: Cliente %d (%d no total) chegou ao restaurante.\n", *id_cliente, clientes_criados + 1);
            pthread_mutex_unlock(&mutex_printf);
            
            clientes_criados++; // Incrementa o contador de clientes do dia
            tempo_aberto++;
        }

        //Fim do Dia
        pthread_mutex_lock(&mutex_restaurante);
        estado_restaurante = FECHADO;
        pthread_mutex_lock(&mutex_printf);
        printf("\nGerente: FIM DO DIA %d! Restaurante FECHADO para novos clientes.\n", dia_da_semana);
        pthread_mutex_unlock(&mutex_printf);
        
        // Acorda todas as threads que podem estar dormindo
        pthread_cond_broadcast(&cond_pedido_feito);
        pthread_cond_broadcast(&cond_prato_pronto);
        pthread_cond_broadcast(&cond_estoque_disponivel);
        pthread_cond_broadcast(&cond_mesa_suja);
        pthread_cond_broadcast(&cond_demanda_por_mesas);

        pthread_mutex_unlock(&mutex_restaurante);

        //Espera todos os clientes saírem antes de terminar o dia
        pthread_mutex_lock(&mutex_printf);
        printf("Gerente: Esperando os %d clientes do dia irem embora...\n", clientes_criados);
        pthread_mutex_unlock(&mutex_printf);
        for (int i = 0; i < clientes_criados; i++) {
            pthread_join(thread_clientes[i], NULL);
        }
        pthread_mutex_lock(&mutex_printf);
        printf("Gerente: Todos os clientes sairam. Encerrando o dia.\n");
        pthread_mutex_unlock(&mutex_printf);

        // Resetar o estado para o proximo dia
        clientes_criados = 0;
        dia_da_semana++;
        mesas_ativas = 0; // Reseta as mesas ativas para o proximo dia
        mesas_ocupadas = 0;
        mesas_sujas = 0;
        clientes_esperando = 0;
        pedidos_count = 0;
        pratos_prontos_count = 0;
        
        if (dia_da_semana > 5 && dia_da_semana <= 7) { // Sábado (6) e Domingo (7)
             pthread_mutex_lock(&mutex_printf);
             printf("Gerente: FIM DE SEMANA! Restaurante fechado.\n");
             pthread_mutex_unlock(&mutex_printf);
             sleep(5); // Simula o fim de semana
        }
    }

    pthread_mutex_lock(&mutex_printf);
    printf("Gerente: O restaurante encerrou suas operacoes permanentemente.\n");
    pthread_mutex_unlock(&mutex_printf);
    return NULL;
}