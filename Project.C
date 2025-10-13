#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

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
    int igredientes[MAX_IGREDIENTES];
    int quantidade_igredientes;
} Receita;

// -- VARIÁVEIS GLOBAIS COMPARTILHADAS -- \\

//Sincronização
pthread_mutex_t mutex_mesas;
pthread_mutex_t mutex_pedidos;
pthread_mutex_t mutex_pratos_prontos;
pthread_mutex_t mutex_estoque;
pthread_mutex_t mutex_restaurante;

pthread_cond_t cond_mesa_livre;
pthread_cond_t cond_pedido_feito;
pthread_cond_t cond_prato_pronto;
pthread_cond_t cond_estoque_disponivel;
pthread_cond_t cond_mesa_suja;

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

    // 2. Inicializar recursos
    pthread_mutex_init(&mutex_mesas, NULL);
    pthread_mutex_init(&mutex_pedidos, NULL);
    pthread_mutex_init(&mutex_pratos_prontos, NULL);
    pthread_mutex_init(&mutex_estoque, NULL);
    pthread_mutex_init(&mutex_restaurante, NULL);

    pthread_cond_init(&cond_mesa_livre, NULL);
    pthread_cond_init(&cond_pedido_feito, NULL);
    pthread_cond_init(&cond_prato_pronto, NULL);
    pthread_cond_init(&cond_estoque_disponivel, NULL);
    pthread_cond_init(&cond_mesa_suja, NULL);

    //Alocar memória para as estruturas
    mesas = (Mesa*) malloc(num_mesas_max * sizeof(Mesa));
    fila_pedidos = (Pedido*) malloc(num_clientes_max *sizeof(Pedido));
    fila_pratos_prontos = (Pedido*) malloc(num_clientes_max * sizeof(Pedido));

    // 3. Criar as threads
    pthread_t thread_cozinheiros[NUM_COZINHEIROS];
    pthread_t thread_garcons[NUM_GARCONS];
    pthread_t thread_limpeza[NUM_LIMPEZA];
    pthread_t thread_estoquista;
    pthread_t thread_gestor_mesas;
    pthread_t thread_gerente_dia;
    pthread_t thread_clientes[num_clientes_max];

    //Criar threads para entidades que existem continuamente
    for (int i = 0; i < NUM_COZINHEIROS; i++) {
        pthread_create(&thread_cozinheiros[i], NULL, cozinheiro, NULL);
    }

    for (int i = 0; i < NUM_GARCONS; i++) {
        pthread_create(&thread_garcons[i], NULL, garcom, NULL);
    }

    for (int i = 0; i < NUM_LIMPEZA; i++) {
        pthread_create(&thread_limpeza[i], NULL, responsavel_limpeza, NULL);
    }

    pthread_create(&thread_estoquista, NULL, estoquista, NULL);
    pthread_create(&thread_gestor_mesas, NULL, gestor_mesas, NULL);
    pthread_create(&thread_gerente_dia, NULL, gerente_dia, NULL);

    pthread_join(thread_gerente_dia, NULL);

    return 0;
}

// -- IMPLEMENTAÇÃO DAS FUNÇÕES DAS ENTIDADES -- \\

int mesas_ativas = 0;                       // Variável para rastrear o número de mesas ativas
int clientes_esperando = 0;                 // Variável para rastrear o número de clientes que não conseguiram sentar
pthread_cond_t cond_demanda_por_mesas;

void *cliente(void *arg) {
    int id_cliente = *((int *)arg);
    free(arg); // Libera memória alocada para o id

    //Tempo de paciência do cliente para esperar uma mesa
    struct timespec tempo_limite;
    int mesa_alocada = -1;

    // 1. Cliente tenta conseguir uma mesa
    pthread_mutex_lock(&mutex_mesas);

    //Se não houver mesas livres, o cliente entra na fila de espera
    while (mesas_ocupadas >= num_mesas_max) {
        printf("Cliente %d: Não tem mesas disponivéis, vou esperar na fila!", id_cliente);
        //Sinaliza o gestor de mesas que há demanda
        clientes_esperando++;
        pthread_cond_signal(&cond_demanda_por_mesas);

        //Define um tempo limite de 5 segundos para a espera
        clock_gettime(CLOCK_REALTIME, &tempo_limite);
        tempo_limite.tv_sec += 5;

        int status = pthread_cond_timedwait(&cond_mesa_livre, &mutex_mesas, &tempo_limite);

        //Se o tempo esgotar, o cliente desiste e vai embora
        if (status == ETIMEDOUT) {
            printf("Cliente %d: Sem paciência! Foi embora porque não tinha mesas.\n", id_cliente);
            clientes_esperando--;
            pthread_mutex_unlock(&mutex_mesas);
            return NULL;
        }
    }

    //Se o cliente chegou até aqui, uma mesa está disponivel. Ele a ocupa
    for (int i = 0; i < num_mesas_max; i++) {
        if (mesas[i].status == LIVRE) {
            mesas[i].status == OCUPADA;
            mesa_alocada = i;
            mesas_ocupadas++;
            printf("Cliente %d: Consegui uma mesa. Mesa %d ocupada. Total de mesas ocupadas: %d.\n", id_cliente, mesa_alocada, mesas_ocupadas);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_mesas);

    // 2. Cliente faz o pedido
    // (A logica de colocar o pedido na fila virá depois)
    printf("Cliente %d: Fazendo o pedido na mesa %d.\n", id_cliente, mesa_alocada);
    sleep(1);                                                                               // Simula o tempo para fazer o pedido

    // 3. Cliente espera o prato
    // (A logica de esperar pelo prato virá depois)
    printf("Cliente %d: Esperando prato...\n", id_cliente);
    sleep(3);                                                                               // Simula o tempo de espera pela comida

    // 4. Cliente come e vai embora
    printf("Cliente %d: Comendo... delicia!\n", id_cliente);
    sleep(5); // Simula o tempo comendo                                                     // Simula o tempo comendo

    // 5. Cliente sinaliza que está saindo e libera a mesa
    pthread_mutex_lock(&mutex_mesas);
    mesas_ocupadas--;

    // 1 = mesa suja, 0 = mesa limpa (Vamos colocar um pouco de aleatoriedade para ver se o cliente foi educado)
    int mesa_suja = rand() % 2; 

    if (mesa_suja) {
        mesas[mesa_alocada].status = SUJA;
        printf("Cliente %d: Saiu. Mesa %d agora esta suja. Mesas ocupadas: %d.\n", id_cliente, mesa_alocada, mesas_ocupadas);
        pthread_cond_signal(&cond_mesa_suja); // Sinaliza para a limpeza
    } else {
        mesas[mesa_alocada].status = LIVRE;
        printf("Cliente %d: Saiu. Mesa %d agora esta livre (e ainda limpa). Mesas ocupadas: %d.\n", id_cliente, mesa_alocada, mesas_ocupadas);
    }
    pthread_mutex_unlock(&mutex_mesas);

    return NULL;
}

void *cozinheiro(void *arg) {
    //Lógica do cozinheiro
}

void *garcom(void *arg) {
    //Lógica do garçom
}

void *responsavel_limpeza(void *arg) {
    //Lógica do responsável pela limpeza
}

void *estoquista(void *arg) {   
    //Lógica do estoquista
}

void *gestor_mesas(void *arg) {
    while (estado_restaurante == ABERTO) {
        pthread_mutex_lock(&mutex_mesas);

        // O gestor espera até que haja um cliente esperando E haja capacidade para adicionar uma nova mesa.
        while (clientes_esperando == 0 && mesas_ocupadas >= mesas_ativas) {
            // Este wait faz o gestor dormir ate ser acordado por um cliente ou por uma limpeza
            pthread_cond_wait(&cond_demanda_por_mesas, &mutex_mesas); 
        }

        // Se houver um cliente esperando e o restaurante não atingiu a capacidade máxima
        if (clientes_esperando > 0 && mesas_ativas < num_mesas_max) {
            mesas_ativas++;
            mesas[mesas_ativas - 1].id_mesa = mesas_ativas - 1;
            mesas[mesas_ativas - 1].status = LIVRE;
            
            printf("Gestor de Mesas: Nova mesa %d adicionada. Total de mesas: %d.\n", mesas_ativas - 1, mesas_ativas);
            
            clientes_esperando--;                       // Reduz a contagem de clientes esperando
        }

        // Sinaliza para os clientes que uma mesa está livre
        pthread_cond_broadcast(&cond_mesa_livre);
        pthread_mutex_unlock(&mutex_mesas);
        
        sleep(2);
    }

    printf("Gestor de Mesas: Encerrando operação...\n");
    return NULL;
}

void *gerente_dia(void *arg) {
    //Lógica do gerente do dia
}