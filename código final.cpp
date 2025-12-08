#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INTERVALOdMEDICAO 1000 //Medição a cada 1 segundo
#define TAMANHO_FILTRO 4 //FILTRO PARA MEDIÇÃO ESTÁVEL
#define PULSOS_POR_VOLTA 2   //Sensor de rotação


// CONTROLE DO MOTOR (PWM)
void configuraMotor(void) {

    DDRB |= (1 << PB1);                    // PB1 = Arduino D9 (saída PWM)
    TCCR1A = (1 << WGM10) | (1 << COM1A1); // Fast PWM 8-bit, não-invertido  256
    TCCR1B = (1 << WGM12) | (1 << CS11);   // Prescaler 8  f +- 7.8khz
    OCR1A = 0;                             // Duty inicial 0% (motor parado)
}

void configuraVelMotor(uint8_t vel) {
    //define razão de ciclo PWM de 0 a 100%
    //proteção contra valores inválidos
    if (vel > 100) vel = 100; //limita a 100%
    OCR1A = (vel * 255) / 100;     //converte % para valor PWM
}

// SENSOR DE ROTAÇÃO (RPM) 
volatile uint32_t totPulso = 0;           // Contador de pulsos
volatile uint8_t flag_medir_rpm = 0;          // Flag para cálculo do RPM
volatile uint16_t pulsos_ultimo_periodo = 0;  // Pulsos no último intervalo

ISR(INT1_vect) {

    // Interrupção do sensor - PD3 (INT1)
    totPulso++;  // Incrementa a cada pulso do sensor
}

void configSensorRotacao(void) {
  //Configura sensor no pino PD3 
    DDRD &= ~(1 << PD3);     // PD3 como entrada
    PORTD |= (1 << PD3);     // Ativa pull-up interno
    EICRA |= (1 << ISC11);   // Interrupção na borda de descida
    EICRA &= ~(1 << ISC10);
    EIMSK |= (1 << INT1);    // Habilita INT1
}

// TEMPORIZADOR PARA MEDIÇÃO - 
void timerMedicao(void) {
    //Timer para medição periódica (1 segundo)
    TCCR2A = (1 << WGM21);    // Modo CTC
    TCCR2B = (1 << CS22);     // Prescaler 64
    OCR2A = 249;              // 1ms (16MHz/64/250)
    TIMSK2 |= (1 << OCIE2A);  // Habilita interrupção
}

volatile uint16_t contTemp = 0;

ISR(TIMER2_COMPA_vect) {
   //Periodicidade de 1 segundo para envio de informações
    contTemp++;
    if (contTemp >= INTERVALOdMEDICAO) {  // 1000ms = 1 segundo
        contTemp = 0;
        pulsos_ultimo_periodo = totPulso;  // Salva pulsos do último segundo
        totPulso = 0;                      // Zera para próximo segundo
        flag_medir_rpm = 1;                    // Sinaliza cálculo do RPM
    }
}


//COMUNICAÇÃO SERIAL
void configuraComunicaSerial(void) {
    UBRR0H = 0;
    UBRR0L = 103;  //9600 bauds para 16MHz
                   //Cálculo: (16000000 / (16 * 9600)) - 1 = 103
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);  //Habilita TX e RX
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); //8 bits, 1 stop bit
}

void enviaMensagem(const char *texto) {
    //Envia informações pela porta serial
    while (*texto) {
        while (!(UCSR0A & (1 << UDRE0)));  // Aguarda buffer vazio
        UDR0 = *texto++;                    
    }
}

void tranformaNum(uint16_t numero) {
    //Informação legível com precisão
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%u", numero);
    enviaMensagem(buffer);
}


uint8_t confirmaDados(void) {
    return (UCSR0A & (1 << RXC0));  // Verifica se há dado recebido
}

char recebeDados(void) {
    while (!confirmaDados());
    return UDR0;  // Lê caractere recebido
}

// FILTRO DE MÉDIA MÓVEL
uint16_t filtrar_medida_rpm(uint16_t nova_medida) {
    // Filtro para medição mais estável do RPM
    static uint16_t historico_rpm[TAMANHO_FILTRO] = {0};
    static uint8_t indice = 0;
    static uint8_t cheio = 0;
    
    historico_rpm[indice] = nova_medida;
    indice = (indice + 1) % TAMANHO_FILTRO;
    
    if (!cheio && indice == 0) cheio = 1;
    
    uint32_t soma = 0;
    uint8_t elementos;

    if (cheio) {
      elementos = TAMANHO_FILTRO;
      } else {
    elementos = indice;
     }
    
    for (uint8_t i = 0; i < elementos; i++) {
        soma += historico_rpm[i];
    }
    
    return (uint16_t)(soma / elementos);
}

//PROGRAMA PRINCIPAL 
int main(void) {
    configuraMotor();              //motor PWM
    configSensorRotacao();     //Sensor RPM
    timerMedicao();  //Temporizador para medições
    configuraComunicaSerial();    //Comunicação serial
    sei();                           //Habilita interrupções globais

    
    //VARIÁVEIS DE CONTROLE
    uint8_t vel_atual = 0;    //vel atual do motor
    uint8_t vel_desejada = 0; //vel solicitada pelo usuário
    uint16_t rpm_medido = 0;         //RPM atual medido
    uint8_t aceleracao_taxa = 5;     //taxa de aceleração

    //BUFFER PARA COMANDOS SERIAL
    char comando[10] = {0};
    uint8_t indice_comando = 0;

    //MENSAGEM INICIAL
    enviaMensagem("Sistema de Controle de Motor DC\r\n");
    enviaMensagem("Comandos: 0-100 (vel%) ou STOP\r\n");
    enviaMensagem("====================================\r\n");

  while (1) {
        //PROCESSAMENTO DE COMANDOS SERIAL
        if (confirmaDados()) {
            char entrada = recebeDados();
            
            //Receber comandos em texto
            if (entrada >= '0' && entrada <= '9' && indice_comando < 9) {
                comando[indice_comando++] = entrada;  // Acumula dígitos
            }
            //comando para parada
            else if ((entrada == 'S' || entrada == 's') && indice_comando == 0) {
                vel_desejada = 0;
                enviaMensagem("Motor PARADO\r\n");
            }
            // Processa comando numérico quando recebe ENTER
            else if ((entrada == '\r' || entrada == '\n') && indice_comando > 0) {
                comando[indice_comando] = '\0';
                uint16_t valor = atoi(comando);
                
                // Tratamento de erros
                if (valor <= 100) {
                    vel_desejada = valor;
                    enviaMensagem("vel ajustada para ");
                    tranformaNum(valor);
                    enviaMensagem("%\r\n");
                } else {
                    enviaMensagem("ERRO: vel deve ser 0-100%\r\n");
                }
                
                indice_comando = 0;
                memset(comando, 0, sizeof(comando));
            }
            ///Comando inválido
            else if (entrada != '\r' && entrada != '\n') {
                enviaMensagem("ERRO: Comando invalido\r\n");
                indice_comando = 0;
                memset(comando, 0, sizeof(comando));
            }
        }
   // CÁLCULO E FILTRAGEM DO RPM
        
        if (flag_medir_rpm) {
            flag_medir_rpm = 0;
            
            //cálculo do RPM
            rpm_medido = (pulsos_ultimo_periodo * 60UL) / PULSOS_POR_VOLTA;
            rpm_medido = filtrar_medida_rpm(rpm_medido);
            
            //Envio periódico de informações
            static uint8_t contador_exibicao = 0;
            if (++contador_exibicao >= 1) {  // A cada segundo
                contador_exibicao = 0;
                enviaMensagem("PWM: ");
                tranformaNum(vel_atual);     
                enviaMensagem("% | RPM: ");
                tranformaNum(rpm_medido);        
                enviaMensagem("\r\n");
            }
        }
        
        //  CONTROLE SUAVE DE vel
        static uint16_t temporizador_rampa = 0;
        
        if (temporizador_rampa >= 50) {  // A cada 50ms
            temporizador_rampa = 0;
            
            //Controle de aceleração (não muda bruscamente)
            if (vel_atual < vel_desejada) {
                vel_atual++;  // Acelera gradualmente
            } else if (vel_atual > vel_desejada) {
                vel_atual--;  // Desacelera gradualmente
            }
            
            // Aplica nova vel ao motor
            configuraVelMotor(vel_atual);
        }
        
        // Delay principal do loop
        _delay_ms(10);
        temporizador_rampa += 10;
    }
	
	return 0;
}