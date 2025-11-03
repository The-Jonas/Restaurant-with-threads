#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>                            // Para threads
#include <semaphore.h>                          // Para semáforos 
#include <unistd.h>                             // Para sleep

/*=======================================================*
 *=========== VARIÁVEIS GLOBAIS E DEFINIÇÕES ============*
 *=======================================================*/

// Inputs do usuário

int MAX_CLIENTES_POR_DIA;
int MAX_MESAS_RESTAURANTE;
int MAX_ESTOQUE_PRATO;

#define TOTAL_PRATOS 5;                                                 // Total de pratos/receitas diferentes no restaurante

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

pthread_mutex_t mutex_print;                                            // Mutex pra controlar a impressão no console 


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
    for (dia_atual = DOMINGO; dia_atual <= SABADO; dia_atual = static_cast<DiaDaSemana>(dia_atual + 1)) {

        pthread_mutex_lock(&mutex_print);
        printf("\n==================== DIA %d ====================\n", dia_atual);
        pthread_mutex_unlock(&mutex_print);
        
        // O restaurante não funciona no fim de semana (Domingo e Sábado)
        if (dia_atual == DOMINGO || dia_atual == SABADO) {
            pthread_mutex_lock(&mutex_print);
            printf("Restaurante fechado (FIM DE SEMANA).\n");
            pthread_mutex_unlock(&mutex_print);
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

        pthread_mutex_lock(&mutex_print);
        printf("--- Fim do Dia %d ---\n", dia_atual);
        pthread_mutex_unlock(&mutex_print);
    }

    // 3. Fim da simulação
    printf("\n=================================================\n");
    printf("======== Simulacao de 7 dias encerrada. ========\n");
    printf("=================================================\n");

    // 4. Destruir mutexes e semaforos
    pthread_mutex_destroy(&mutex_print);

    return 0;
}

 /*=======================================================*
  *============== IMPLEMENTAÇÃO DAS FUNÇÕES ==============*
  *=======================================================*/