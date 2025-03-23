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

// Declaração da função restart_system antes de ser usada
void restart_system(uint8_t *buf, struct render_area *frame_area);

// Declaração da função play_alert_sound antes de sua utilização
void play_alert_sound(uint pin);

// Definição dos pinos
const uint I2C_SDA_PIN = 14;  // Pino do barramento I2C (dados)
const uint I2C_SCL_PIN = 15;  // Pino do barramento I2C (clock)
const uint BUZZER_A = 21;     // Pino para o buzzer A
const uint BUZZER_B = 10;
const uint BUTTON_A = 5;      // Pino para o botão A
const uint BUTTON_B = 6;      // Pino para o botão B
const uint LEDvr = 12;        // Pino para o LED 12 (agora será o LEDv)
const uint LEDa = 11;         // Pino para o LED 11 (LED do botão A)
const uint LEDv = 13;         // Pino para o LED 13 (agora será o LEDvr)

// Configurações do PWM
const float DIVISOR_CLK_PWM = 16.0;      
const uint16_t PERIOD = 2000;            
const uint16_t MAX_WRAP_DIV_BUZZER = 16; 
const uint16_t MIN_WRAP_DIV_BUZZER = 2;  

// Estados do botão
typedef enum {
    IDLE, DEBOUNCING_A, RELEASE_A, DEBOUNCING_B, RELEASE_B, ACTION_A, ACTION_B      
} state_button;

// Variáveis globais
uint16_t wrap_div_buzzer = 8; 
bool is_buzzer_a_playing = true; // Começa com o som tocando

// Função para tocar uma nota no buzzer
void play_note(uint pin, uint16_t wrap) {
    int slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, wrap);
    pwm_set_gpio_level(pin, wrap / wrap_div_buzzer);
    pwm_set_enabled(slice, true);
}

// Função para silenciar o buzzer
void play_rest(uint pin) {
    int slice = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice, false);
}

// Função para desenhar texto na tela
void draw_text_display(const char *text[], uint8_t *buf, struct render_area *frame_area) {
    int y = 0;
    for (uint i = 0; i < 4; i++) { 
        WriteString(buf, 5, y, (char *)text[i]);
        y += 8;
    }
    render(buf, frame_area);
}

// Função para ler os botões com debounce
void read_buttons(uint8_t *buf, struct render_area *frame_area) {
    static state_button s = IDLE;    
    static uint cnt = 0;              // contador para o debounce
    const uint DEBOUNCE_CYCLES = 50;  // tempo de debounce

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
        case IDLE:
            if (gpio_get(BUTTON_A) == 0) {  
                draw_text_display(textBTN_A, buf, frame_area);
                s = DEBOUNCING_A;
            }          
            if (gpio_get(BUTTON_B) == 0) { 
                draw_text_display(textBTN_B, buf, frame_area);
                s = DEBOUNCING_B;
            }          
            cnt = 0;
            break;
        case DEBOUNCING_A:
            if (gpio_get(BUTTON_A) == 0) {
                cnt++;
                if (cnt > DEBOUNCE_CYCLES) {
                    cnt = 0;
                    s = RELEASE_A;
                }
            } else {
                s = IDLE;
            }
            break;
        case DEBOUNCING_B:
            if (gpio_get(BUTTON_B) == 0) {
                cnt++;
                if (cnt > DEBOUNCE_CYCLES) {
                    cnt = 0;
                    s = RELEASE_B;
                }
            } else {
                s = IDLE;
            }
            break;
        case RELEASE_A:
            if (gpio_get(BUTTON_A) == 1)
                s = ACTION_A;
            break;
        case RELEASE_B:
            if (gpio_get(BUTTON_B) == 1)
                s = ACTION_B;
            break;
        case ACTION_A:
            s = IDLE;
            gpio_put(LEDvr, 0);  // Desliga LED 12 (LEDvr)
            gpio_put(LEDv, 0);    // Desliga LED 13 (LEDv)
            gpio_put(LEDa, 1);   // Acende LED 11 (LEDa)
            play_rest(BUZZER_A); // Desliga o som (A) quando o botão A é pressionado
            is_buzzer_a_playing = false; 
            break;
        case ACTION_B:
            s = IDLE;
            gpio_put(LEDa, 0);   // Desliga LED 11 (LEDa)
            gpio_put(LEDvr, 1);  // Acende LED 12 (LEDvr)
            play_rest(BUZZER_A);  
            is_buzzer_a_playing = false; 

            // Espera 5 segundos antes de reiniciar o sistema
            sleep_ms(5000);
            
            // Reinicia o sistema, como se tivesse acabado de ser ligado
            gpio_put(LEDvr, 0);  // Desliga LED 12 (LEDvr)
            gpio_put(LEDa, 0);   // Desliga LED 11 (LEDa)

            // Reinicializa a tela e os componentes
            restart_system(buf, frame_area);  // Chama a função que reinicializa a tela corretamente

            // Reativar o som quando o sistema reiniciar
            play_alert_sound(BUZZER_A);  // Reinicia o som
            is_buzzer_a_playing = true;

            // **Aqui está a modificação**: Acende o LED azul (LEDvr) após o reinício
            gpio_put(LEDvr, 1);  // Acende o LED 12 (LEDvr)

            break;
        default:
            s = IDLE;
            cnt = 0;
    }
}

// Função para reiniciar o sistema e renderizar o display corretamente
void restart_system(uint8_t *buf, struct render_area *frame_area) {
    // Reinicializa a tela
    SSD1306_init();

    // Limpa o buffer e renderiza
    memset(buf, 0, SSD1306_BUF_LEN);
    render(buf, frame_area);

    // Exibe a nova mensagem após reinício
    const char *restart_text[] = {
        "  OBRIGADO ATÉ  ",
        "  O PROXIMO     ",
        "   HORARIO      ",
        "               "
    };
    draw_text_display(restart_text, buf, frame_area);

    // Aguarda 5 segundos antes de iniciar o sistema novamente
    sleep_ms(5000);
}

// Função para tocar o alerta inicial (3 notas)
void play_alert_sound(uint pin) {
    uint melody_alert[] = {NOTE_C4, NOTE_E4, NOTE_G4};  // 3 notas de alerta
    uint durations_alert[] = {500, 500, 500};           // Duração de 500 ms para cada nota
    int repeat_count = 0;  // Contador para as repetições

    while (repeat_count < 3) {  // Repetir a melodia 3 vezes
        for (int i = 0; i < 3; i++) {
            play_note(pin, melody_alert[i]);
            sleep_ms(durations_alert[i]);  // Espera a duração da nota
        }
        repeat_count++;  // Incrementa a contagem de repetições
        sleep_ms(500);  // Pausa entre os ciclos de melodia
    }
    play_rest(pin);  // Quando terminar de tocar as 3 vezes, a melodia para
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

    gpio_pull_up(BUTTON_A);
    gpio_pull_up(BUTTON_B);

    // Acende o LED 13 (LEDv) ao iniciar
    gpio_put(LEDv, 1);  // LED 13 aceso ao iniciar
    gpio_put(LEDa, 0);

    slice = pwm_gpio_to_slice_num(BUZZER_A);
    pwm_set_clkdiv(slice, DIVISOR_CLK_PWM);
    pwm_set_wrap(slice, PERIOD);
    pwm_set_enabled(slice, true);
    
    // Começa a tocar o som automaticamente ao ligar
    play_alert_sound(BUZZER_A);  // Alerta sonoro
    is_buzzer_a_playing = true;
}

// Função para ler os valores do joystick
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
        return true;
    }
    return false;
}

// Função principal
int main() {
    stdio_init_all();
    setup_audio();

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
            gpio_put(LEDvr, 1); // Acende LED 12 (LEDvr)
            while (!gpio_get(BUTTON_A)) {
                // Aguarda o botão A ser pressionado
                sleep_ms(100);
            }
            gpio_put(LEDvr, 0);  // Desliga LED 12
            // Continua o código após pressionar o botão A
            read_buttons(buf, &frame_area);
        }
        else {
            read_buttons(buf, &frame_area);  // Normal interação com botões
        }
    }

    return 0;
}
