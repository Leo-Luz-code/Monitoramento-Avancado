#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"

#include "lwip/tcp.h"
#include "hardware/i2c.h"

// Inclusão das bibliotecas dos periféricos
#include "aht20.h"
#include "bmp280.h"
#include "ssd1306.h"
#include "font.h"

// --- CONFIGURAÇÕES ---
#define WIFI_SSID "Seu_usuario_wifi"
#define WIFI_PASS "Sua_senha_wifi"

// Botão para entrar em modo BOOTSEL (opcional, mas recomendado)
#define BOOTSEL_BUTTON 6

// I2C dos Sensores (BMP280, AHT20)
#define I2C_PORT_SENSORS i2c0
#define I2C_SDA_SENSORS 0
#define I2C_SCL_SENSORS 1

// I2C do Display OLED (SSD1306)
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define DISP_ADDR 0x3C

#define SEA_LEVEL_PRESSURE 101325.0 // Pressão ao nível do mar em Pascal (Pa)

// Variáveis globais para armazenar os dados dos sensores
float temperature_bmp = 0.0f;
float pressure_kpa = 0.0f;
float altitude_m = 0.0f;
float temperature_aht = 0.0f;
float humidity_rh = 0.0f;

// --- ESTRUTURA E CONTEÚDO DA PÁGINA WEB ---

// Estrutura para gerenciar o estado da resposta HTTP
struct http_state
{
    char response[4096];
    size_t len;
    size_t sent;
};

// Conteúdo HTML/JavaScript para a página de monitoramento com gráficos
const char HTML_BODY[] =
    "<!DOCTYPE html>"
    "<html lang=\"pt-BR\">"
    "<head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>Monitoramento - Pi Pico</title>"
    "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>"
    "<style>"
    "body { font-family: sans-serif; margin: 0; padding: 20px; background-color: #f4f4f4; }"
    ".container { max-width: 1200px; margin: auto; }"
    ".chart-container { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; }"
    "h1, h2 { text-align: center; color: #333; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class=\"container\">"
    "<h1>📊 Estação de Monitoramento com Raspberry Pi Pico W</h1>"
    "<div class=\"chart-container\">"
    "<h2>Temperaturas (°C)</h2>"
    "<canvas id=\"tempChart\"></canvas>"
    "</div>"
    "<div class=\"chart-container\">"
    "<h2>Pressão (kPa) e Altitude (m)</h2>"
    "<canvas id=\"pressureAltitudeChart\"></canvas>"
    "</div>"
    "<div class=\"chart-container\">"
    "<h2>Umidade Relativa (%)</h2>"
    "<canvas id=\"humidityChart\"></canvas>"
    "</div>"
    "</div>"
    "<script>"
    "const MAX_DATA_POINTS = 30;"
    "function createChart(ctx, type, labels, datasets) {"
    "return new Chart(ctx, {"
    "type: type,"
    "data: { labels: labels, datasets: datasets },"
    "options: {"
    "responsive: true,"
    "scales: { x: { display: true }, y: { beginAtZero: false } },"
    "animation: { duration: 500 }"
    "}"
    "});"
    "}"
    "const tempChart = createChart(document.getElementById('tempChart').getContext('2d'), 'line', [], ["
    "{ label: 'Temperatura (BMP280)', borderColor: 'rgb(255, 99, 132)', backgroundColor: 'rgba(255, 99, 132, 0.5)', tension: 0.2, data: [] },"
    "{ label: 'Temperatura (AHT20)', borderColor: 'rgb(54, 162, 235)', backgroundColor: 'rgba(54, 162, 235, 0.5)', tension: 0.2, data: [] }"
    "]);"
    "const pressureAltitudeChart = createChart(document.getElementById('pressureAltitudeChart').getContext('2d'), 'line', [], ["
    "{ label: 'Pressão (kPa)', borderColor: 'rgb(75, 192, 192)', backgroundColor: 'rgba(75, 192, 192, 0.5)', data: [], yAxisID: 'y' },"
    "{ label: 'Altitude (m)', borderColor: 'rgb(153, 102, 255)', backgroundColor: 'rgba(153, 102, 255, 0.5)', data: [], yAxisID: 'y1' }"
    "]);"
    "pressureAltitudeChart.options.scales.y1 = { type: 'linear', display: true, position: 'right', grid: { drawOnChartArea: false } };"
    "const humidityChart = createChart(document.getElementById('humidityChart').getContext('2d'), 'line', [], ["
    "{ label: 'Umidade (AHT20)', borderColor: 'rgb(255, 205, 86)', backgroundColor: 'rgba(255, 205, 86, 0.5)', tension: 0.2, data: [] }"
    "]);"
    "function addData(chart, label, data) {"
    "chart.data.labels.push(label);"
    "data.forEach((value, index) => {"
    "chart.data.datasets[index].data.push(value);"
    "});"
    "if (chart.data.labels.length > MAX_DATA_POINTS) {"
    "chart.data.labels.shift();"
    "chart.data.datasets.forEach(dataset => {"
    "dataset.data.shift();"
    "});"
    "}"
    "chart.update();"
    "}"
    "async function updateCharts() {"
    "try {"
    "const response = await fetch('/sensordata');"
    "const data = await response.json();"
    "const time = new Date().toLocaleTimeString();"
    "addData(tempChart, time, [data.temp_bmp, data.temp_aht]);"
    "addData(pressureAltitudeChart, time, [data.pressure, data.altitude]);"
    "addData(humidityChart, time, [data.humidity]);"
    "} catch (error) {"
    "console.error(\"Erro ao buscar dados:\", error);"
    "}"
    "}"
    "setInterval(updateCharts, 5000);"
    "window.onload = updateCharts;"
    "</script>"
    "</body>"
    "</html>";

// --- FUNÇÕES AUXILIARES E DE REDE ---

// Função para o botão de BOOTSEL
void gpio_irq_handler(uint gpio, uint32_t events)
{
    if (gpio == BOOTSEL_BUTTON)
    {
        reset_usb_boot(0, 0);
    }
}

// Função para calcular a altitude a partir da pressão
float calculate_altitude_func(float pressure_pa)
{
    return 44330.0 * (1.0 - pow(pressure_pa / SEA_LEVEL_PRESSURE, 0.1903));
}

// Callback executado quando os dados são enviados com sucesso
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

// Callback para lidar com requisições HTTP recebidas
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    if (strstr(req, "GET /sensordata"))
    {
        // Endpoint para fornecer os dados dos sensores em JSON
        char json_payload[256];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"temp_bmp\":%.2f,\"pressure\":%.2f,\"altitude\":%.2f,\"temp_aht\":%.2f,\"humidity\":%.2f}",
                                temperature_bmp, pressure_kpa, altitude_m, temperature_aht, humidity_rh);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n\r\n%s",
                           json_len, json_payload);
    }
    else
    {
        // Endpoint para a página principal HTML
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n\r\n%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    pbuf_free(p);
    return ERR_OK;
}

// Callback para aceitar novas conexões TCP
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

// Inicia o servidor HTTP
static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb || tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao iniciar o servidor HTTP.\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP iniciado na porta 80.\n");
}

// --- FUNÇÃO PRINCIPAL ---

int main()
{
    stdio_init_all();
    sleep_ms(2000); // Aguarda a inicialização do terminal serial

    // Configura botão de BOOTSEL
    gpio_init(BOOTSEL_BUTTON);
    gpio_set_dir(BOOTSEL_BUTTON, GPIO_IN);
    gpio_pull_up(BOOTSEL_BUTTON);
    gpio_set_irq_enabled_with_callback(BOOTSEL_BUTTON, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Inicializa I2C para os sensores
    i2c_init(I2C_PORT_SENSORS, 400 * 1000);
    gpio_set_function(I2C_SDA_SENSORS, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_SENSORS, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_SENSORS);
    gpio_pull_up(I2C_SCL_SENSORS);

    // Inicializa I2C para o display
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    // Inicializa o display SSD1306
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, DISP_ADDR, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando...", 0, 0);
    ssd1306_send_data(&ssd);

    // Inicializa os sensores
    bmp280_init(I2C_PORT_SENSORS);
    struct bmp280_calib_param params;
    bmp280_get_calib_params(I2C_PORT_SENSORS, &params);

    aht20_reset(I2C_PORT_SENSORS);
    aht20_init(I2C_PORT_SENSORS);

    // Conexão Wi-Fi
    if (cyw43_arch_init())
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi: FALHA init", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    ssd1306_draw_string(&ssd, "Conectando...", 0, 10);
    ssd1306_send_data(&ssd);

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 15000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi: ERRO conexao", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    // Wi-Fi conectado, mostra o IP no display
    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi: OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    // Inicia o servidor web
    start_http_server();

    // Loop principal de monitoramento
    char buffer[20];
    while (true)
    {
        cyw43_arch_poll(); // Mantém a conexão Wi-Fi ativa

        // Leitura do BMP280
        int32_t raw_temp, raw_press;
        bmp280_read_raw(I2C_PORT_SENSORS, &raw_temp, &raw_press);
        int32_t temp_c = bmp280_convert_temp(raw_temp, &params);
        int32_t press_pa = bmp280_convert_pressure(raw_press, raw_temp, &params);

        // Leitura do AHT20
        AHT20_Data aht_data;
        aht20_read(I2C_PORT_SENSORS, &aht_data);

        // Atualiza as variáveis globais
        temperature_bmp = temp_c / 100.0f;
        pressure_kpa = press_pa / 1000.0f;
        altitude_m = calculate_altitude_func(press_pa);
        temperature_aht = aht_data.temperature;
        humidity_rh = aht_data.humidity;

        // Exibe os dados no display OLED
        ssd1306_fill_part(&ssd, false, 0, 20, 128, 44); // Limpa a área dos dados

        snprintf(buffer, sizeof(buffer), "T1:%.1fC P:%.1fkPa", temperature_bmp, pressure_kpa);
        ssd1306_draw_string(&ssd, buffer, 0, 22);

        snprintf(buffer, sizeof(buffer), "T2:%.1fC U:%.1f%%", temperature_aht, humidity_rh);
        ssd1306_draw_string(&ssd, buffer, 0, 34);

        snprintf(buffer, sizeof(buffer), "Alt: %.1fm", altitude_m);
        ssd1306_draw_string(&ssd, buffer, 0, 46);

        ssd1306_send_data(&ssd);

        sleep_ms(2000); // Aguarda 2 segundos antes da próxima leitura
    }

    cyw43_arch_deinit();
    return 0;
}