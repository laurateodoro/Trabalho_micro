#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


void ajustar_velocidade_motor(uint8_t velocidade) {
    //Define razão de ciclo PWM de 0 a 100%
    //Proteção contra valores inválidos
    if (velocidade > 100) velocidade = 100; //Limita a 100%
    OCR1A = (velocidade * 255UL) / 100;     //Converte % para valor PWM
}

int main(void) {
    // Configurações iniciais virão aqui
    while (1) {
        // Loop principal Parte da Laura
    }
    return 0;
}