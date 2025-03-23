#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "ssd1306.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "notes.h"
#include "hardware/adc.h"

// Declaração de funções
void restart_system(uint8_t *buf, struct render_area *frame_area);  // Reinicia o sistema
void play_alert_sound(uint pin);  // Toca um som de alerta no buzzer

// Definição dos pinos utilizados
const uint I2C_SDA_PIN = 14;  // Pino SDA do I2C
const uint I2C_SCL_PIN = 15;  // Pino SCL do I2C
const uint BUZZER_A = 21;     // Pino para o buzzer A
const uint BUZZER_B = 10;     // Pino para o buzzer B
const uint BUTTON_A = 5;      // Pino para o botão A
const uint BUTTON_B = 6;      // Pino para o botão B
const uint LEDvr = 12;        // Pino para o LED 12 (LED de presença)
const uint LEDa = 11;         // Pino para o LED 11 (LED do botão A)
const uint LEDv = 13;         // Pino para o LED 13 (LED de confirmação)

// Configurações de PWM (modulação por largura de pulso)
const float DIVISOR_CLK_PWM = 16.0;      
const uint16_t PERIOD = 2000;            
const uint16_t MAX_WRAP_DIV_BUZZER = 16; 
const uint16_t MIN_WRAP_DIV_BUZZER = 2;  

// Estados do botão, para controle de debounce
typedef enum {
    IDLE, DEBOUNCING_A, RELEASE_A, DEBOUNCING_B, RELEASE_B, ACTION_A, ACTION_B      
} state_button;

// Variáveis globais
uint16_t wrap_div_buzzer = 8;  // Valor padrão de divisão do buzzer
bool is_buzzer_a_playing = true; // Flag para controle do buzzer A

// Função que toca uma nota no buzzer
void play_note(uint pin, uint16_t wrap) {
    int slice = pwm_gpio_to_slice_num(pin);  // Obtém o número do "slice" do pino
    pwm_set_wrap(slice, wrap);  // Define o valor do wrap
    pwm_set_gpio_level(pin, wrap / wrap_div_buzzer);  // Define o nível do pino
    pwm_set_enabled(slice, true);  // Habilita o PWM
}

// Função para silenciar o buzzer
void play_rest(uint pin) {
    int slice = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice, false);  // Desativa o PWM (som)
}

// Função que desenha texto no display
void draw_text_display(const char *text[], uint8_t *buf, struct render_area *frame_area) {
    int y = 0;
    for (uint i = 0; i < 4; i++) { 
        WriteString(buf, 5, y, (char *)text[i]);  // Escreve o texto no buffer
        y += 8;  // Incrementa o Y para o próximo texto
    }
    render(buf, frame_area);  // Renderiza o conteúdo do buffer
}

// Função que lê o estado dos botões com debounce
void read_buttons(uint8_t *buf, struct render_area *frame_area) {
    static state_button s = IDLE;    
    static uint cnt = 0;              // Contador para o debounce
    const uint DEBOUNCE_CYCLES = 50;  // Tempo de debounce

    const char *textBTN_A[] = {
        "   PRESENCA    ",
        "  CONFIRMADA   ",
        "    PRESS B    ",
        "   (COLETE)    "
    };

    const char *textBTN_B[] = {                         
        "   PRONTO      ",
        "  DADOS SENDO  ",
        "  REINICIADOS  ",
        "    AGUARDE    "
    };

    switch (s) {
        case IDLE:  // Estado inicial
            if (gpio_get(BUTTON_A) == 0) {  
                draw_text_display(textBTN_A, buf, frame_area);  // Exibe mensagem do botão A
                s = DEBOUNCING_A;  // Muda para estado de debounce do botão A
            }          
            if (gpio_get(BUTTON_B) == 0) { 
                draw_text_display(textBTN_B, buf, frame_area);  // Exibe mensagem do botão B
                s = DEBOUNCING_B;  // Muda para estado de debounce do botão B
            }          
            cnt = 0;  // Reseta o contador de debounce
            break;
        case DEBOUNCING_A:  // Espera o botão A ser liberado
            if (gpio_get(BUTTON_A) == 0) {
                cnt++;  // Conta o tempo de debounce
                if (cnt > DEBOUNCE_CYCLES) {
                    cnt = 0;
                    s = RELEASE_A;  // Quando o botão A for pressionado, muda para RELEASE_A
                }
            } else {
                s = IDLE;  // Se o botão A foi solto, volta para o estado IDLE
            }
            break;
        case DEBOUNCING_B:  // Espera o botão B ser liberado
            if (gpio_get(BUTTON_B) == 0) {
                cnt++;  // Conta o tempo de debounce
                if (cnt > DEBOUNCE_CYCLES) {
                    cnt = 0;
                    s = RELEASE_B;  // Quando o botão B for pressionado, muda para RELEASE_B
                }
            } else {
                s = IDLE;  // Se o botão B foi solto, volta para o estado IDLE
            }
            break;
        case RELEASE_A:  // Quando o botão A for solto
            if (gpio_get(BUTTON_A) == 1)
                s = ACTION_A;  // Muda para ação do botão A
            break;
        case RELEASE_B:  // Quando o botão B for solto
            if (gpio_get(BUTTON_B) == 1)
                s = ACTION_B;  // Muda para ação do botão B
            break;
        case ACTION_A:  // Ação associada ao botão A
            s = IDLE;
            gpio_put(LEDvr, 0);  // Desliga o LED 12
            gpio_put(LEDv, 0);    // Desliga o LED 13
            gpio_put(LEDa, 1);   // Acende o LED 11
            play_rest(BUZZER_A); // Desliga o buzzer A
            is_buzzer_a_playing = false;  // Desliga o buzzer A
            break;
        case ACTION_B:  // Ação associada ao botão B
            s = IDLE;
            gpio_put(LEDa, 0);   // Desliga o LED 11
            gpio_put(LEDvr, 1);  // Acende o LED 12
            play_rest(BUZZER_A);  
            is_buzzer_a_playing = false; 

            sleep_ms(5000);  // Espera 5 segundos antes de reiniciar o sistema

            restart_system(buf, frame_area);  // Chama a função que reinicializa o sistema

            play_alert_sound(BUZZER_A);  // Toca novamente o alerta
            is_buzzer_a_playing = true;

            gpio_put(LEDvr, 1);  // Acende o LED 12 após reiniciar
            break;
        default:
            s = IDLE;  // Reseta o estado para IDLE
            cnt = 0;  // Reseta o contador de debounce
    }
}

// Função para reiniciar o sistema e renderizar o display corretamente
void restart_system(uint8_t *buf, struct render_area *frame_area) {
    SSD1306_init();  // Inicializa o display SSD1306

    memset(buf, 0, SSD1306_BUF_LEN);  // Limpa o buffer
    render(buf, frame_area);  // Renderiza o conteúdo do buffer

    // Exibe a mensagem de reinício
    const char *restart_text[] = {
        "  OBRIGADO ATÉ  ",
        "  O PROXIMO     ",
        "   HORARIO      ",
        "               "
    };
    draw_text_display(restart_text, buf, frame_area);

    sleep_ms(5000);  // Aguarda 5 segundos antes de reiniciar o sistema
}

// Função para tocar o alerta sonoro (3 notas)
void play_alert_sound(uint pin) {
    uint melody_alert[] = {NOTE_C4, NOTE_E4, NOTE_G4};  // Melodia de 3 notas
    uint durations_alert[] = {500, 500, 500};           // Duração de 500 ms para cada nota
    int repeat_count = 0;  // Contador de repetições

    while (repeat_count < 3) {  // Repetir a melodia 3 vezes
        for (int i = 0; i < 3; i++) {
            play_note(pin, melody_alert[i]);  // Toca cada nota
            sleep_ms(durations_alert[i]);  // Espera a duração de cada nota
        }
        repeat_count++;  // Incrementa o contador de repetições
        sleep_ms(500);  // Pausa entre as repetições
    }
    play_rest(pin);  // Desliga o buzzer
}

// Função para configurar o áudio e os pinos
void setup_audio() {
    uint slice;
    stdio_init_all();

    gpio_set_function(LEDv, GPIO_FUNC_SIO);
    gpio_set_function(LEDa, GPIO_FUNC_SIO);
    gpio_set_function(LEDvr, GPIO_FUNC_SIO);
    gpio_set_function(BUTTON_A, GPIO_FUNC_SIO);
    gpio_set_function(BUTTON_B, GPIO_FUNC_SIO);
    gpio_set_function(BUZZER_A, GPIO_FUNC_PWM);
    gpio_set_function(BUZZER_B, GPIO_FUNC_PWM);

    gpio_set_dir(LEDv, GPIO_OUT);
    gpio_set_dir(LEDa, GPIO_OUT);
    gpio_set_dir(LEDvr, GPIO_OUT);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_set_dir(BUTTON_B, GPIO_IN);

    gpio_pull_up(BUTTON_A);  // Ativa o pull-up interno no botão A
    gpio_pull_up(BUTTON_B);  // Ativa o pull-up interno no botão B

    gpio_put(LEDv, 1);  // Acende o LED 13 (LEDv) ao iniciar
    gpio_put(LEDa, 0);  // Desliga o LED 11

    slice = pwm_gpio_to_slice_num(BUZZER_A);  // Obtém o slice do buzzer
    pwm_set_clkdiv(slice, DIVISOR_CLK_PWM);  // Define o divisor de clock
    pwm_set_wrap(slice, PERIOD);  // Define o valor do wrap do PWM
    pwm_set_enabled(slice, true);  // Habilita o PWM

    play_alert_sound(BUZZER_A);  // Toca o som de alerta ao ligar
    is_buzzer_a_playing = true;
}

// Função para ler os valores do joystick (não está sendo usada no código atual)
bool read_joystick() {
    uint joystick_x, joystick_y;

    // Inicializa ADC
    adc_init();
    adc_gpio_init(26);  // GPIO26 para eixo X
    adc_gpio_init(27);  // GPIO27 para eixo Y

    // Lê os valores dos eixos
    adc_select_input(0);
    joystick_x = adc_read();

    adc_select_input(1);
    joystick_y = adc_read();

    // Se o joystick for movido (valores diferentes do centro)
    if (joystick_x < 2000 || joystick_x > 4000 || joystick_y < 2000 || joystick_y > 4000) {
        return true;  // Retorna true se o joystick for movido
    }
    return false;  // Caso contrário, retorna false
}

// Função principal
int main() {
    stdio_init_all();  // Inicializa o sistema de entrada e saída

    setup_audio();  // Configura o áudio e os pinos

    // Configura o display SSD1306
    i2c_init(i2c1, SSD1306_I2C_CLK * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    SSD1306_init();  // Inicializa o display corretamente ao ligar

    struct render_area frame_area = {0, SSD1306_WIDTH - 1, 0, SSD1306_NUM_PAGES - 1};
    calc_render_area_buflen(&frame_area);

    uint8_t buf[SSD1306_BUF_LEN];
    memset(buf, 0, SSD1306_BUF_LEN);
    render(buf, &frame_area);

    const char *startup_text[] = {
        "   APERTE O    ",
        "  BOTÃO A PARA ",
        "  CONFIRMAR A  ",
        "    PRESENÇA   "
    };

    // Exibe a mensagem inicial na tela
    draw_text_display(startup_text, buf, &frame_area);

    // Aguarda 5 segundos antes de iniciar o sistema
    sleep_ms(5000);

    // Começa a interação com os botões e joystick
    while (true) {
        if (read_joystick()) {
            gpio_put(LEDvr, 1); // Acende LED 12 (LEDvr) quando o joystick é movido
            while (!gpio_get(BUTTON_A)) {
                sleep_ms(100);  // Aguarda o botão A ser pressionado
            }
            gpio_put(LEDvr, 0);  // Desliga LED 12 após pressionar o botão A
            read_buttons(buf, &frame_area);  // Continua a leitura dos botões
        } else {
            read_buttons(buf, &frame_area);  // Caso o joystick não seja movido, só interage com os botões
        }
    }

    return 0;
}
