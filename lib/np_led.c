#include "np_led.h"
#include "ws2818b.pio.h"

npLED_t leds[LED_COUNT]; // Buffer de LEDs
PIO np_pio;
uint sm;

int SORRISO_NORMAL[5][5][3] = {
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 12, 0}, {0, 0, 0}, {0, 12, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 12, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 12, 0}},
    {{0, 0, 0}, {0, 12, 0}, {0, 12, 0}, {0, 12, 0}, {0, 0, 0}}};

/**
 * Inicializa a máquina PIO para controle da matriz de LEDs.
 */
void npInit(uint pin)
{
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;

    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0)
    {
        np_pio = pio1;
        sm = pio_claim_unused_sm(np_pio, true);
    }

    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);

    for (uint i = 0; i < LED_COUNT; ++i)
    {
        leds[i].R = leds[i].G = leds[i].B = 0;
    }
}

/**
 * Atribui uma cor RGB a um LED.
 */
void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < LED_COUNT)
    {
        leds[index].R = r;
        leds[index].G = g;
        leds[index].B = b;
    }
}

/**
 * Limpa o buffer de pixels.
 */
void npClear()
{
    for (uint i = 0; i < LED_COUNT; ++i)
    {
        npSetLED(i, 0, 0, 0);
    }
}

/**
 * Escreve os dados do buffer nos LEDs.
 */
void npWrite()
{
    for (uint i = 0; i < LED_COUNT; ++i)
    {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
}

/**
 * Aplica um fator de escala para brilho na matriz.
 */
void applyBrightnessToMatrix(int matriz[5][5][3], float scale)
{
    for (int linha = 0; linha < 5; linha++)
    {
        for (int coluna = 0; coluna < 5; coluna++)
        {
            matriz[linha][coluna][0] = (uint8_t)(matriz[linha][coluna][0] * scale);
            matriz[linha][coluna][1] = (uint8_t)(matriz[linha][coluna][1] * scale);
            matriz[linha][coluna][2] = (uint8_t)(matriz[linha][coluna][2] * scale);
        }
    }
}

/**
 * Converte uma posição na matriz para índice no vetor de LEDs.
 */
int getIndex(int x, int y)
{
    return (y % 2 == 0) ? 24 - (y * 5 + x) : 24 - (y * 5 + (4 - x));
}

/**
 * Atualiza a matriz com base em uma configuração de cores.
 */
void updateMatrix(int matriz[5][5][3])
{
    for (int linha = 0; linha < 5; linha++)
    {
        for (int coluna = 0; coluna < 5; coluna++)
        {
            int posicao = getIndex(coluna, linha);
            npSetLED(posicao, matriz[linha][coluna][0], matriz[linha][coluna][1], matriz[linha][coluna][2]);
        }
    }
    npWrite();
}

/**
 * Escreve a letra T na matriz de LEDs
 * @param color Cor RGB no formato [R, G, B] (opcional, branco por padrão)
 */
void drawT(uint8_t color[3])
{
    int matriz[5][5][3] = {0};

    // Padrão da letra T
    for (int col = 0; col < 5; col++)
    {
        matriz[0][col][0] = color ? color[0] : 12; // R
        matriz[0][col][1] = color ? color[1] : 12; // G
        matriz[0][col][2] = color ? color[2] : 12; // B
    }
    for (int row = 1; row < 5; row++)
    {
        matriz[row][2][0] = color ? color[0] : 12; // R
        matriz[row][2][1] = color ? color[1] : 12; // G
        matriz[row][2][2] = color ? color[2] : 12; // B
    }

    updateMatrix(matriz);
}

/**
 * Escreve a letra P na matriz de LEDs
 * @param color Cor RGB no formato [R, G, B] (opcional, branco por padrão)
 */
void drawP(uint8_t color[3])
{
    int matriz[5][5][3] = {0};

    // Padrão da letra P
    for (int row = 0; row < 5; row++)
    {
        matriz[row][0][0] = color ? color[0] : 12; // R
        matriz[row][0][1] = color ? color[1] : 12; // G
        matriz[row][0][2] = color ? color[2] : 12; // B
    }
    for (int col = 1; col < 4; col++)
    {
        matriz[0][col][0] = color ? color[0] : 12;
        matriz[0][col][1] = color ? color[1] : 12;
        matriz[0][col][2] = color ? color[2] : 12;
        matriz[2][col][0] = color ? color[0] : 12;
        matriz[2][col][1] = color ? color[1] : 12;
        matriz[2][col][2] = color ? color[2] : 12;
    }
    matriz[1][4][0] = color ? color[0] : 12;
    matriz[1][4][1] = color ? color[1] : 12;
    matriz[1][4][2] = color ? color[2] : 12;

    updateMatrix(matriz);
}

/**
 * Escreve a letra A na matriz de LEDs
 * @param color Cor RGB no formato [R, G, B] (opcional, branco por padrão)
 */
void drawA(uint8_t color[3])
{
    int matriz[5][5][3] = {0};

    // Padrão da letra A
    for (int row = 0; row < 5; row++)
    {
        matriz[row][0][0] = color ? color[0] : 12; // R
        matriz[row][0][1] = color ? color[1] : 12; // G
        matriz[row][0][2] = color ? color[2] : 12; // B
        matriz[row][4][0] = color ? color[0] : 12;
        matriz[row][4][1] = color ? color[1] : 12;
        matriz[row][4][2] = color ? color[2] : 12;
    }
    for (int col = 1; col < 4; col++)
    {
        matriz[0][col][0] = color ? color[0] : 12;
        matriz[0][col][1] = color ? color[1] : 12;
        matriz[0][col][2] = color ? color[2] : 12;
        matriz[2][col][0] = color ? color[0] : 12;
        matriz[2][col][1] = color ? color[1] : 12;
        matriz[2][col][2] = color ? color[2] : 12;
    }

    updateMatrix(matriz);
}

/**
 * Escreve a letra U na matriz de LEDs
 * @param color Cor RGB no formato [R, G, B] (opcional, branco por padrão)
 */
void drawU(uint8_t color[3])
{
    int matriz[5][5][3] = {0};

    // Padrão da letra U
    for (int row = 0; row < 4; row++)
    {
        matriz[row][0][0] = color ? color[0] : 12; // R
        matriz[row][0][1] = color ? color[1] : 12; // G
        matriz[row][0][2] = color ? color[2] : 12; // B
        matriz[row][4][0] = color ? color[0] : 12;
        matriz[row][4][1] = color ? color[1] : 12;
        matriz[row][4][2] = color ? color[2] : 12;
    }
    for (int col = 1; col < 4; col++)
    {
        matriz[4][col][0] = color ? color[0] : 12;
        matriz[4][col][1] = color ? color[1] : 12;
        matriz[4][col][2] = color ? color[2] : 12;
    }

    updateMatrix(matriz);
}

/**
 * Desenha um sorriso normal na matriz de LEDs :)
 */
void drawSorrisoNormal()
{
    updateMatrix(SORRISO_NORMAL);
}