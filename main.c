#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"

#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"

#include "aht20.h"
#include "bmp280.h"
#include "ssd1306.h"
#include "np_led.h"
#include "font.h"

// --- CONFIGURAÇÕES DE REDE E HARDWARE ---
#define WIFI_SSID "sua_rede_wifi"
#define WIFI_PASS "sua_senha_wifi"

#define RESET_CONFIG_BUTTON 5
#define BOOTSEL_BUTTON 6
#define LED_GREEN_PIN 11
#define LED_RED_PIN 13
#define BUZZER_A 21
#define MATRIX_LED_PIN 7

#define I2C_PORT_SENSORS i2c0
#define I2C_SDA_SENSORS 0
#define I2C_SCL_SENSORS 1

#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define DISP_ADDR 0x3C

#define SEA_LEVEL_PRESSURE 101325.0

// --- VARIÁVEIS GLOBAIS ---

// Variáveis para armazenar os dados dos sensores (já com offset)
float temperature_bmp = 0.0f, pressure_kpa = 0.0f, altitude_m = 0.0f;
float temperature_aht = 0.0f, humidity_rh = 0.0f;

// Variáveis para as configurações (limites e offsets)
float temp_offset = 0.0f, pressure_offset_kpa = 0.0f;
float temp_min = 0.0f, temp_max = 40.0f;
float pressure_min = 80.0f, pressure_max = 105.0f;
float altitude_min = -100.0f, altitude_max = 1000.0f;
float humidity_min = 20.0f, humidity_max = 90.0f;

// Configurações do buzzer
bool buzzer_state = false;      // Estado atual do buzzer (ligado/desligado)
uint32_t last_buzzer_time = 0;  // Último momento que o buzzer foi acionado
const float DIVIDER_PWM = 16.0; // Divisor de clock para PWM do buzzer
const uint16_t PERIOD = 4096;   // Período do PWM para o buzzer
uint slice_buzzer;              // Slice PWM associado ao buzzer

static volatile uint32_t current_time; // Tempo atual (usado para debounce)
static volatile uint32_t last_time_button = 0;

// Variáveis para indicar se tudo está OK
static bool last_ok_state = false;
static uint64_t last_update_time = 0;

// --- CONTEÚDO DA PÁGINA WEB ---

struct http_state
{
    char response[8192];
    size_t len;
    size_t sent;
};

const char HTML_BODY[] =
    "<!DOCTYPE html>"
    "<html lang='pt-BR'>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Monitoramento Avançado - Pi Pico</title>"
    "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
    "<style>"
    "body { font-family: system-ui, -apple-system, sans-serif; margin: 0; padding: 1rem; background-color: #f0f2f5; color: #333; }"
    ".grid-container { display: grid; grid-template-columns: repeat(auto-fit, minmax(350px, 1fr)); gap: 1rem; max-width: 1400px; margin: auto; }"
    ".card { background-color: #fff; padding: 1.5rem; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); }"
    "h1, h2 { color: #1a2b47; text-align: center; margin-top: 0; }"
    "select, input, button { width: 100%; padding: 0.75rem; border: 1px solid #ddd; border-radius: 8px; font-size: 1rem; box-sizing: border-box; margin-top: 0.5rem; }"
    "button { background-color: #007aff; color: white; border: none; cursor: pointer; transition: background-color 0.2s; font-weight: bold; }"
    "button:hover { background-color: #0056b3; }"
    "label { font-weight: 600; color: #555; }"
    "fieldset { border: 1px solid #ddd; border-radius: 8px; padding: 1rem; margin-top: 1rem; }"
    "legend { font-weight: bold; padding: 0 0.5rem; color: #007aff; }"
    ".form-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 1rem; }"
    ".value-display { font-size: 1.5rem; font-weight: bold; color: #007aff; text-align: center; margin: 0.5rem 0; }"
    ".value-display.out-of-range { color: #ff3b30; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='grid-container'>"
    "<div class='card'>"
    "<h2>📊 Gráfico de Monitoramento</h2>"
    "<label for='chart-select'>Selecione o Gráfico:</label>"
    "<select id='chart-select'>"
    "<option value='temperatures' selected>Temperaturas (°C)</option>"
    "<option value='pressure'>Pressão (kPa)</option>"
    "<option value='altitude'>Altitude (m)</option>"
    "<option value='humidity'>Umidade (%)</option>"
    "</select>"
    "<canvas id='mainChart' style='margin-top: 1rem;'></canvas>"
    "</div>"
    "<div class='card'>"
    "<h2>⚙️ Configurações</h2>"
    "<form id='settings-form'>"
    "<fieldset>"
    "<legend>Calibração (Offsets)</legend>"
    "<div class='form-grid'>"
    "<div><label for='temp_offset'>Temp. (°C)</label><input type='number' step='0.1' id='temp_offset' name='temp_offset'></div>"
    "<div><label for='pressure_offset_kpa'>Pressão (kPa)</label><input type='number' step='0.01' id='pressure_offset_kpa' name='pressure_offset_kpa'></div>"
    "</div>"
    "</fieldset>"
    "<fieldset>"
    "<legend>Limites de Alerta</legend>"
    "<div class='form-grid'>"
    "<div><label for='temp_min'>Temp Min</label><input type='number' step='1' id='temp_min' name='temp_min'></div>"
    "<div><label for='temp_max'>Temp Max</label><input type='number' step='1' id='temp_max' name='temp_max'></div>"
    "<div><label for='pressure_min'>Pressão Min</label><input type='number' step='0.1' id='pressure_min' name='pressure_min'></div>"
    "<div><label for='pressure_max'>Pressão Max</label><input type='number' step='0.1' id='pressure_max' name='pressure_max'></div>"
    "<div><label for='altitude_min'>Altitude Min</label><input type='number' step='10' id='altitude_min' name='altitude_min'></div>"
    "<div><label for='altitude_max'>Altitude Max</label><input type='number' step='10' id='altitude_max' name='altitude_max'></div>"
    "<div><label for='humidity_min'>Umidade Min</label><input type='number' step='1' id='humidity_min' name='humidity_min'></div>"
    "<div><label for='humidity_max'>Umidade Max</label><input type='number' step='1' id='humidity_max' name='humidity_max'></div>"
    "</div>"
    "</fieldset>"
    "<button type='submit' style='margin-top: 1rem;'>Salvar Configurações</button>"
    "</form>"
    "<div id='live-values' style='margin-top: 1rem; text-align: center;'></div>"
    "</div>"
    "</div>"
    "<script>"
    "const MAX_DATA_POINTS = 20;"
    "let chartInstance;"
    "const form = document.getElementById('settings-form');"
    "const chartSelect = document.getElementById('chart-select');"

    "const chartConfigs = {"
    "'temperatures': { type: 'line', data: { labels: [], datasets: [{ label: 'Temp (BMP280)', data: [], borderColor: '#ff6384' }, { label: 'Temp (AHT20)', data: [], borderColor: '#36a2eb' }] } },"
    "'pressure': { type: 'line', data: { labels: [], datasets: [{ label: 'Pressão (kPa)', data: [], borderColor: '#4bc0c0' }] } },"
    "'altitude': { type: 'line', data: { labels: [], datasets: [{ label: 'Altitude (m)', data: [], borderColor: '#9966ff' }] } },"
    "'humidity': { type: 'line', data: { labels: [], datasets: [{ label: 'Umidade (%)', data: [], borderColor: '#ffcd56' }] } }"
    "};"

    "function createOrUpdateChart() {"
    "if (chartInstance) chartInstance.destroy();"
    "const selected = chartSelect.value;"
    "const config = chartConfigs[selected];"
    "config.options = { responsive: true, animation: { duration: 400 }, scales: { y: { beginAtZero: false } } };"
    "chartInstance = new Chart(document.getElementById('mainChart').getContext('2d'), config);"
    "}"

    "function updateDisplayValues(data) {"
    "const s = data.sensors;"
    "const set = data.settings;"
    "document.getElementById('live-values').innerHTML = "
    "`<h2>Valores Atuais</h2>` + "
    "`<p>Temp BMP280: <span class='value-display' id='v_temp_bmp'>${s.temp_bmp.toFixed(2)} °C</span></p>` + "
    "`<p>Temp AHT20: <span class='value-display'>${s.temp_aht.toFixed(2)} °C</span></p>` + "
    "`<p>Pressão: <span class='value-display' id='v_pressure'>${s.pressure.toFixed(2)} kPa</span></p>` + "
    "`<p>Altitude: <span class='value-display' id='v_altitude'>${s.altitude.toFixed(1)} m</span></p>` + "
    "`<p>Umidade: <span class='value-display' id='v_humidity'>${s.humidity.toFixed(1)} %</span></p>`;"

    "document.getElementById('v_temp_bmp').classList.toggle('out-of-range', s.temp_bmp < set.temp_min || s.temp_bmp > set.temp_max);"
    "document.getElementById('v_pressure').classList.toggle('out-of-range', s.pressure < set.pressure_min || s.pressure > set.pressure_max);"
    "document.getElementById('v_altitude').classList.toggle('out-of-range', s.altitude < set.altitude_min || s.altitude > set.altitude_max);"
    "document.getElementById('v_humidity').classList.toggle('out-of-range', s.humidity < set.humidity_min || s.humidity > set.humidity_max);"
    "}"

    "async function updateData() {"
    "try {"
    "const response = await fetch('/sensordata');"
    "const data = await response.json();"
    "updateDisplayValues(data);"
    "const time = new Date().toLocaleTimeString();"

    "if (chartInstance.data.labels.length >= MAX_DATA_POINTS) {"
    "chartInstance.data.labels.shift();"
    "chartInstance.data.datasets.forEach(d => d.data.shift());"
    "}"
    "chartInstance.data.labels.push(time);"
    "const s = data.sensors;"
    "const selectedChart = chartSelect.value;"
    "if(selectedChart === 'temperatures') { chartInstance.data.datasets[0].data.push(s.temp_bmp); chartInstance.data.datasets[1].data.push(s.temp_aht); }"
    "if(selectedChart === 'pressure') { chartInstance.data.datasets[0].data.push(s.pressure); }"
    "if(selectedChart === 'altitude') { chartInstance.data.datasets[0].data.push(s.altitude); }"
    "if(selectedChart === 'humidity') { chartInstance.data.datasets[0].data.push(s.humidity); }"
    "chartInstance.update('none');"
    "} catch (e) { console.error('Falha ao buscar dados:', e); }"
    "}"

    "async function loadInitialSettings() {"
    "const response = await fetch('/sensordata');"
    "const data = await response.json();"
    "const set = data.settings;"
    "for (const key in set) { if(document.getElementById(key)) document.getElementById(key).value = set[key]; }"
    "}"

    "form.addEventListener('submit', (e) => {"
    "e.preventDefault();"
    "const formData = new FormData(form);"
    "const params = new URLSearchParams();"
    "for(const pair of formData) { params.append(pair[0], pair[1]); }"
    "fetch(`/set_settings?${params.toString()}`).then(res => {"
    "if(res.ok) alert('Configurações salvas!'); else alert('Erro ao salvar.');"
    "});"
    "});"

    "chartSelect.addEventListener('change', createOrUpdateChart);"
    "window.onload = () => { createOrUpdateChart(); loadInitialSettings(); setInterval(updateData, 2000); };"
    "</script>"
    "</body>"
    "</html>";

// --- FUNÇÕES DE REDE E LÓGICA ---

void ligar_led_verde()
{
    gpio_put(LED_GREEN_PIN, 1);
    gpio_put(LED_RED_PIN, 0);
}

void ligar_led_vermelho()
{
    gpio_put(LED_GREEN_PIN, 0);
    gpio_put(LED_RED_PIN, 1);
}

// Funções para controle do buzzer
void buzzer_on()
{
    pwm_set_gpio_level(BUZZER_A, 300); // Define nível PWM para ligar o buzzer
    buzzer_state = true;
}

void buzzer_off()
{
    pwm_set_gpio_level(BUZZER_A, 0); // Desliga o buzzer
    buzzer_state = false;
}

void gpio_irq_handler(uint gpio, uint32_t events)
{
    current_time = to_us_since_boot(get_absolute_time());

    if (gpio == BOOTSEL_BUTTON)
        reset_usb_boot(0, 0);

    if (current_time - last_time_button > 200000)
    {
        last_time_button = current_time;

        if (gpio == RESET_CONFIG_BUTTON)
        {
            // Reseta as configurações para os valores padrão
            temp_offset = 0.0f;
            pressure_offset_kpa = 0.0f;
            temp_min = 0.0f;
            temp_max = 40.0f;
            pressure_min = 80.0f;
            pressure_max = 105.0f;
            altitude_min = -100.0f;
            altitude_max = 1000.0f;
            humidity_min = 20.0f;
            humidity_max = 90.0f;
        }
    }
}

float calculate_altitude_func(float pressure_pa)
{
    return 44330.0 * (1.0 - pow(pressure_pa / SEA_LEVEL_PRESSURE, 0.1903));
}

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

// Função para extrair um parâmetro float de uma string de requisição GET
void parse_float_param(const char *req, const char *param, float *value)
{
    char *found = strstr(req, param);
    if (found)
    {
        *value = atof(found + strlen(param));
    }
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    hs->sent = 0;

    if (strstr(req, "GET /set_settings?"))
    {
        parse_float_param(req, "temp_offset=", &temp_offset);
        parse_float_param(req, "pressure_offset_kpa=", &pressure_offset_kpa);
        parse_float_param(req, "temp_min=", &temp_min);
        parse_float_param(req, "temp_max=", &temp_max);
        parse_float_param(req, "pressure_min=", &pressure_min);
        parse_float_param(req, "pressure_max=", &pressure_max);
        parse_float_param(req, "altitude_min=", &altitude_min);
        parse_float_param(req, "altitude_max=", &altitude_max);
        parse_float_param(req, "humidity_min=", &humidity_min);
        parse_float_param(req, "humidity_max=", &humidity_max);

        char response_body[1500];
        snprintf(response_body, sizeof(response_body),
                 "Request: %s\n"
                 "Configuracoes atualizadas:\n"
                 "temp_offset=%.2f\n"
                 "pressure_offset_kpa=%.2f\n"
                 "temp_min=%.2f\n"
                 "temp_max=%.2f\n"
                 "pressure_min=%.2f\n"
                 "pressure_max=%.2f\n"
                 "altitude_min=%.2f\n"
                 "altitude_max=%.2f\n"
                 "humidity_min=%.2f\n"
                 "humidity_max=%.2f\n",
                 req,
                 temp_offset,
                 pressure_offset_kpa,
                 temp_min,
                 temp_max,
                 pressure_min,
                 pressure_max,
                 altitude_min,
                 altitude_max,
                 humidity_min,
                 humidity_max);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n\r\n"
                           "%s",
                           (int)strlen(response_body), response_body);
    }
    else if (strstr(req, "GET /sensordata"))
    {
        char json_payload[1024];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"sensors\":{\"temp_bmp\":%.2f,\"pressure\":%.2f,\"altitude\":%.1f,\"temp_aht\":%.2f,\"humidity\":%.1f},"
                                "\"settings\":{\"temp_offset\":%.2f,\"pressure_offset_kpa\":%.2f,"
                                "\"temp_min\":%.1f,\"temp_max\":%.1f,\"pressure_min\":%.2f,\"pressure_max\":%.2f,"
                                "\"altitude_min\":%.1f,\"altitude_max\":%.1f,\"humidity_min\":%.1f,\"humidity_max\":%.1f}}",
                                temperature_bmp, pressure_kpa, altitude_m, temperature_aht, humidity_rh,
                                temp_offset, pressure_offset_kpa,
                                temp_min, temp_max, pressure_min, pressure_max,
                                altitude_min, altitude_max, humidity_min, humidity_max);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                           json_len, json_payload);
    }
    else
    { // Serve a página principal
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    tcp_bind(pcb, IP_ADDR_ANY, 80);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
}

int main()
{
    stdio_init_all();
    sleep_ms(1000);

    // Inicialização do Hardware e Wi-Fi
    gpio_init(BOOTSEL_BUTTON);
    gpio_set_dir(BOOTSEL_BUTTON, GPIO_IN);
    gpio_pull_up(BOOTSEL_BUTTON);
    gpio_set_irq_enabled_with_callback(BOOTSEL_BUTTON, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(RESET_CONFIG_BUTTON);
    gpio_set_dir(RESET_CONFIG_BUTTON, GPIO_IN);
    gpio_pull_up(RESET_CONFIG_BUTTON);
    gpio_set_irq_enabled(RESET_CONFIG_BUTTON, GPIO_IRQ_EDGE_FALL, true);

    i2c_init(I2C_PORT_SENSORS, 400 * 1000);
    gpio_set_function(I2C_SDA_SENSORS, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_SENSORS, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_SENSORS);
    gpio_pull_up(I2C_SCL_SENSORS);

    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    // Configura LEDs como saídas
    gpio_init(LED_GREEN_PIN);
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
    gpio_init(LED_RED_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    ligar_led_verde();

    // Configura a matriz de LEDs Neopixel
    npInit(MATRIX_LED_PIN);
    drawSorrisoNormal(); // Desenha sorriso normal

    // Configura PWM para o buzzer
    gpio_set_function(BUZZER_A, GPIO_FUNC_PWM);
    slice_buzzer = pwm_gpio_to_slice_num(BUZZER_A);
    pwm_set_clkdiv(slice_buzzer, DIVIDER_PWM);
    pwm_set_wrap(slice_buzzer, PERIOD);
    pwm_set_gpio_level(BUZZER_A, 0);
    pwm_set_enabled(slice_buzzer, true);

    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, DISP_ADDR, I2C_PORT_DISP);
    ssd1306_config(&ssd);

    bmp280_init(I2C_PORT_SENSORS);
    struct bmp280_calib_param params;
    bmp280_get_calib_params(I2C_PORT_SENSORS, &params);

    aht20_reset(I2C_PORT_SENSORS);
    aht20_init(I2C_PORT_SENSORS);

    cyw43_arch_init();
    cyw43_arch_enable_sta_mode();
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Conectando WiFi...", 0, 0);
    ssd1306_send_data(&ssd);

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 15000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi: ERRO", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi Conectado!", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();

    char buffer[20];
    while (true)
    {
        cyw43_arch_poll();

        // Leitura RAW dos sensores
        int32_t raw_temp_bmp, raw_press;
        bmp280_read_raw(I2C_PORT_SENSORS, &raw_temp_bmp, &raw_press);
        int32_t temp_c = bmp280_convert_temp(raw_temp_bmp, &params);
        int32_t press_pa_raw = bmp280_convert_pressure(raw_press, raw_temp_bmp, &params);

        AHT20_Data aht_data;
        aht20_read(I2C_PORT_SENSORS, &aht_data);

        // *** APLICAÇÃO DOS OFFSETS DE CALIBRAÇÃO ***
        temperature_bmp = (temp_c / 100.0f) + temp_offset;
        pressure_kpa = (press_pa_raw / 1000.0f) + pressure_offset_kpa;
        altitude_m = calculate_altitude_func(pressure_kpa * 1000); // A função espera Pa

        // Dados do AHT20
        temperature_aht = aht_data.temperature + temp_offset;
        humidity_rh = aht_data.humidity;

        // Atualiza display OLED
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, ip_str, 0, 5);
        snprintf(buffer, sizeof(buffer), "T:%.1fC", temperature_bmp);
        ssd1306_draw_string(&ssd, buffer, 0, 15);
        snprintf(buffer, sizeof(buffer), "P:%.1fkPa", pressure_kpa);
        ssd1306_draw_string(&ssd, buffer, 0, 25);
        snprintf(buffer, sizeof(buffer), "U:%.1f%%", humidity_rh);
        ssd1306_draw_string(&ssd, buffer, 0, 35);
        snprintf(buffer, sizeof(buffer), "Alt:%.0fm", altitude_m);
        ssd1306_draw_string(&ssd, buffer, 0, 45);
        ssd1306_send_data(&ssd);

        if (temperature_bmp < temp_min || temperature_bmp > temp_max ||
            temperature_aht < temp_min || temperature_aht > temp_max ||
            pressure_kpa < pressure_min || pressure_kpa > pressure_max ||
            altitude_m < altitude_min || altitude_m > altitude_max ||
            humidity_rh < humidity_min || humidity_rh > humidity_max)
        {
            if (!buzzer_state)
            {
                ligar_led_vermelho();
                buzzer_on();
                last_buzzer_time = time_us_64();

                if (temperature_bmp < temp_min || temperature_bmp > temp_max ||
                    temperature_aht < temp_min || temperature_aht > temp_max)
                {
                    drawT((uint8_t[]){255, 0, 0}); // Desenha T vermelho
                }

                if (pressure_kpa < pressure_min || pressure_kpa > pressure_max)
                {
                    drawP((uint8_t[]){255, 0, 0}); // Desenha P vermelho
                }

                if (altitude_m < altitude_min || altitude_m > altitude_max)
                {
                    drawA((uint8_t[]){255, 0, 0}); // Desenha A vermelho
                }

                if (humidity_rh < humidity_min || humidity_rh > humidity_max)
                {
                    drawU((uint8_t[]){255, 0, 0}); // Desenha U vermelho
                }
            }
            if (buzzer_state && (time_us_64() - last_buzzer_time) >= 250000)
            {
                buzzer_off();
            }
        }
        else
        {
            ligar_led_verde();
            drawSorrisoNormal(); // Desenha sorriso normal
            buzzer_off();
        }

        sleep_ms(250);
    }
}