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

    return 0;
}

// -- IMPLEMENTAÇÃO DAS FUNÇÕES DAS ENTIDADES -- \\ 

void *cliente(void *arg) {
    //Lógica do cliente
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
    //Lógica do gestor de mesas
}

void *gerente_dia(void *arg) {
    //Lógica do gerente do dia
}