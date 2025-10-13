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

int mesas_ativas = 0;                                   // Variável para rastrear o número de mesas ativas
int clientes_esperando = 0;                             // Variável para rastrear o número de clientes que não conseguiram sentar
int ingredientes_em_falta = 0;                          // Quantidade de tipos de ingredientes em falta
pthread_t thread_clientes[200];                         // Array para as threads de clientes
int clientes_criados = 0;
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
        mesas_sujas++;
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
    int id_cozinheiro = *((int *)arg);
    free(arg);

    while (estado_restaurante == ABERTO) {
        pthread_mutex_lock(&mutex_pedidos);
    
        //O cozinheiro espera que haja pedidos na fila
        while (pedidos_count == 0) {
            printf("Cozinheiro %d: Não há pedidos, esperando por trabalho...\n", id_cozinheiro);
            pthread_cond_wait(&cond_pedido_feito, &mutex_pedidos);
        }

        // 1. Pega um pedido da fila e move os outros pedidos pra frente
        Pedido pedido_atual = fila_pedidos[0];
        for (int i = 0; i < pedidos_count - 1; i++) {
            fila_pedidos[i] = fila_pedidos[i+1];
        }
        pedidos_count --;

        printf("Cozinheiro %d: Peguei o pedido do cliente %d. Pedidos na fila: %d.\n", id_cozinheiro, pedido_atual.id_cliente, pedidos_count);
        pthread_mutex_unlock(&mutex_pedidos);

        // 2. Verifica e retira os igredientes (lógica futura)
        printf("Cozinheiro %d: Verificando e retirando igredientes para o prato %d...\n", id_cozinheiro, pedido_atual.id_cliente);

        // 3. Prepara o prato
        printf("Cozinheiro %d: Preparando o prato para o cliente %d...\n", id_cozinheiro, pedido_atual.id_cliente);
        sleep(4);

        pthread_mutex_lock(&mutex_pratos_prontos);

        // 4. Coloca o prato na fila de pratos prontos
        fila_pratos_prontos[pratos_prontos_count] = pedido_atual;
        pratos_prontos_count++;
        printf("Cozinheiro %d: Prato do cliente %d pronto. Pratos na fila: %d.\n", id_cozinheiro, pedido_atual.id_cliente, pratos_prontos_count);

        // 5. Sinaliza para o garçom que há um prato pronto
        pthread_cond_signal(&cond_prato_pronto);
        pthread_mutex_unlock(&mutex_pratos_prontos);
    }
    
    printf("Cozinheiro %d: Encerrando operação...\n", id_cozinheiro);
}

void *garcom(void *arg) {
    int id_garcom = *((int *)arg);
    free(arg);

    while (estado_restaurante == ABERTO){

        // 1. Atender pedidos (lógica futura)

        // 2. Entregar pratos prontos
        pthread_mutex_lock(&mutex_pratos_prontos);

        //O garçom espera que haja pratos prontos na fila
        while (pratos_prontos_count = 0) {
            printf("Garçom %d: Não há pratos prontos, esperando na cozinha...\n", id_garcom);
            pthread_cond_wait(&cond_prato_pronto, &mutex_pratos_prontos);
        }

        Pedido prato_pronto = fila_pratos_prontos[0];
        for (int i = 0; i < pratos_prontos_count - 1; i++) {
            fila_pratos_prontos[i] = fila_pratos_prontos[i+1];
        }
        pratos_prontos_count--;

        printf("Garçom %d: Peguei o prato do cliente %d. Pratos na fila: %d.\n", id_garcom, prato_pronto.id_cliente, pratos_prontos_count);
        pthread_mutex_unlock(&mutex_pratos_prontos);
        
        printf("Garçom %d: Entregando prato para o cliente %d...\n", id_garcom, prato_pronto.id_cliente);
        sleep(2);

        // 3. Receber pagamento e liberar a mesa
        // (A logica de esperar que o cliente termine de comer será adicionada no cliente)
        printf("Garçom %d: Cliente %d terminou de comer. Recebendo pagamento...\n", id_garcom, prato_pronto.id_cliente);
        sleep(1);
    }

    printf("Garçom %d: Encerrando operação...\n", id_garcom);
    return NULL;
}

void *responsavel_limpeza(void *arg) {
    while (estado_restaurante == ABERTO) {
        pthread_mutex_lock(&mutex_mesas);

        //A thread da limpeza espera que haja pelo menos uma mesa suja
        while (mesas_sujas == 0) {
            pthread_cond_wait(&cond_mesa_suja, &mutex_mesas);
        }

        //Procura uma mesa suja para limpar
        for (int i = 0; i < num_mesas_max; i++) {
            if (mesas[i].status == SUJA) {
                //Limpa a mesa
                printf("Responsável pela limpeza: Começando a limpar a mesa %d.\n", i);
                sleep(2);

                //A mesa agora é livre e limpa
                mesas[i].status = LIVRE;
                mesas_sujas--;
                printf("Responsável pela limpeza: Mesa %d limpa. Mesas sujas: %d.\n", i, mesas_sujas);

                //Sinaliza para o gestor de mesas que uma vaga foi liberada
                pthread_cond_signal(&cond_demanda_por_mesas);

                //Sai do loop pra garantir que uma mesa de cada vez seja limpa
                break;
            }
        }
        pthread_mutex_unlock(&mutex_mesas);
    }
    printf("Responsável pela limpeza: Encerrando operação...\n");
    return NULL;
}

void *estoquista(void *arg) {   
    while (estado_restaurante == ABERTO) {
        pthread_mutex_lock(&mutex_estoque);

        //O estoquista espera até que algum tipo de ingrediente esteja em falta
        while (ingredientes_em_falta == 0) {
            pthread_cond_wait(&cond_estoque_disponivel, &mutex_estoque);
        }

        //Se chegou aqui, é porque há ingredientes em falta. Ele vai repor.
        printf("Estoquista: Recebi um aviso! vou verificar o estoque e repor o que for necessário.\n");

        //Lógica de reposição de ingredientes
        for (int i = 0; i < MAX_IGREDIENTES; i++) {
            // Se o nível de um ingrediente estiver abaixo ou igual a 2, ele repõe
            if (estoque[i] <= 2) {
                estoque[i] = 10;
                printf("Estoquista: Repus o ingrediente %d. Nivel agora: %d.\n", i, estoque[i]); 
            }
        }

        // Zera a contagem de ingredientes em falta após repor
        ingredientes_em_falta = 0;
        
        // Avisa os cozinheiros que ingredientes podem estar disponiveis agora
        pthread_cond_broadcast(&cond_estoque_disponivel);

        pthread_mutex_unlock(&mutex_estoque);
    }

    printf("Estoquista: Encerrando operação...\n");
    return NULL;
}

void *gestor_mesas(void *arg) {
    while (estado_restaurante == ABERTO) {
        pthread_mutex_lock(&mutex_mesas);

        //O gestor espera até que haja um cliente esperando E haja capacidade para adicionar uma nova mesa.
        while (clientes_esperando == 0 && mesas_ocupadas >= mesas_ativas) {
            pthread_cond_wait(&cond_demanda_por_mesas, &mutex_mesas); 
        }

        //Se houver um cliente esperando e o restaurante não atingiu a capacidade máxima
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
    while (dia_da_semana <= 7) {
        pthread_mutex_lock(&mutex_restaurante);
        estado_restaurante = ABERTO;
        printf("\nGerente: Novo dia. Hoje e o dia %d. Restaurante ABERTO!\n", dia_da_semana);
        pthread_mutex_unlock(&mutex_restaurante);

        //Simula o tempo de abertura do restaurante durante o dia
        int tempo_aberto = 0;
        while (tempo_aberto < 25 && clientes_criados < num_clientes_max) {
            //Simula a chegada de clientes em intervalos aleátorios (de 2 a 10 segundos)
            sleep(rand() % 10 + 2);

            //Cria a thread do cliente
            int *id_cliente = (int *)malloc(sizeof(int));
            *id_cliente = clientes_criados;

            pthread_create(&thread_clientes[clientes_criados], NULL, cliente, (void *)id_cliente);
            printf("Gerente: Cliente %d chegou ao restaurante.\n", clientes_criados);

            tempo_aberto++;
        }

        //Fim do Dia
        pthread_mutex_lock(&mutex_restaurante);
        estado_restaurante = FECHADO;
        printf("\nGerente: FIM DO DIA! Restaurante FECHADO para novos clientes.\n");
        pthread_mutex_unlock(&mutex_restaurante);

        //Espera todos os clientes saírem antes de terminar o dia
        for (int i = 0; i < clientes_criados; i++) {
        pthread_join(thread_clientes[i], NULL);
        }

        // Resetar o estado para o proximo dia
        clientes_criados = 0;
        dia_da_semana++;
        
        if (dia_da_semana > 5) {
             printf("Gerente: FIM DE SEMANA! Restaurante fechado.\n");
             sleep(12); // Simula o fim de semana
        }
    }
    
    printf("Gerente: O restaurante encerrou suas operacoes permanentemente.\n");
    return NULL
}