/*
 * ================================================================
 *  PetFeeder: Alimentador Pet Automático com Controle Inteligente
 *  de Obstrução
 *  Hardware: Arduino Uno + driver L298N + motor de passo NEMA 17
 * ================================================================
 *
 *  O motor gira uma hélice (rosca) que empurra a ração do
 *  reservatório para o prato.
 *
 *  Funcionamento:
 *   - Modo automático: gira por TEMPO_GIRANDO, para por TEMPO_PARADO
 *     e repete indefinidamente.
 *   - Comandos pelo Monitor Serial da Arduino IDE (9600 baud),
 *     uma letra por comando:
 *       g = girar agora        s = parar agora
 *       a = liga automático    m = desliga automático (só manual)
 *       i = inverte sentido    t = liga/desliga anti-travamento
 *       ? = ajuda / status
 *   - Anti-travamento: durante o giro, o motor avança por
 *     TEMPO_AVANCO e recua por TEMPO_RECUO, repetindo, para soltar
 *     grãos que prendem a hélice.
 *
 *  Ligações (ver README):
 *   D8..D11 -> IN1..IN4 do L298N
 *   5V      -> borne 5V do L298N (jumper 5V-EN removido)
 *   GND     -> borne GND do L298N (junto com o negativo da fonte)
 *
 *  Dependências: apenas a biblioteca Stepper, que já vem com a
 *  Arduino IDE.
 */

#include <Stepper.h>

// ================================================================
//  CONFIGURAÇÃO - ajuste estes valores e grave o código novamente
// ================================================================

// Motor
const int PASSOS_POR_VOLTA = 200;            // NEMA 17 padrão: 1,8° por passo -> 200 passos/volta
const int VELOCIDADE_RPM   = 60;             // rotações por minuto; mais rápido = menos torque

// Ciclo de alimentação
const unsigned long TEMPO_GIRANDO = 10000;   // duração de cada giro (ms)
const unsigned long TEMPO_PARADO  = 5000;    // pausa entre giros no modo automático (ms)
const bool INICIAR_EM_AUTOMATICO  = true;    // modo ao ligar o Arduino

// Anti-travamento: dentro de cada giro, alterna avanço e recuo
const bool ANTI_TRAVAMENTO_INICIAL = true;   // estado ao ligar o Arduino
const unsigned long TEMPO_AVANCO = 1000;     // duração de cada avanço (ms) - a 60 RPM = 1 volta
const unsigned long TEMPO_RECUO  = 300;      // duração de cada recuo (ms)  - a 60 RPM ≈ 1/3 de volta

// Pinos do Arduino ligados às entradas do L298N
const int IN1 = 8;
const int IN2 = 9;
const int IN3 = 10;
const int IN4 = 11;

// Objeto que gera a sequência de passos nos 4 pinos.
// Bobina A do motor em OUT1/OUT2 e bobina B em OUT3/OUT4.
// Se o motor só vibrar sem girar, os pares estão trocados:
// mude a ordem para (IN1, IN3, IN2, IN4) ou corrija os fios.
Stepper motor(PASSOS_POR_VOLTA, IN1, IN2, IN3, IN4);

// ================================================================
//  ESTADO - variáveis que mudam durante a execução
// ================================================================

bool automatico = INICIAR_EM_AUTOMATICO;     // true = ciclo automático ligado
bool girando = false;                        // true = motor em movimento
int sentido = 1;                             // 1 = sentido normal, -1 = invertido
unsigned long inicioEstado = 0;              // instante (ms) em que começou o giro ou a pausa atual

bool antiTravamento = ANTI_TRAVAMENTO_INICIAL;
bool recuando = false;                       // true = fase de recuo do anti-travamento
unsigned long inicioFase = 0;                // instante (ms) em que começou o avanço/recuo atual

// ================================================================
//  FUNÇÕES DO MOTOR
// ================================================================

// Coloca as 4 entradas do L298N em nível baixo, cortando a corrente
// nas bobinas. Usado nas pausas para o motor e o L298N não
// esquentarem à toa (a rosca não precisa de torque parada).
void desligarBobinas() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// Começa um giro. Os passos em si são dados no loop(), um por vez,
// para que o Arduino continue lendo comandos enquanto o motor gira.
void iniciarGiro() {
  if (girando) return;                       // já está girando: ignora
  girando = true;
  recuando = false;                          // todo giro começa avançando
  inicioEstado = millis();
  inicioFase = inicioEstado;
  Serial.println(F("[MOTOR] Girando..."));
}

// Encerra o giro, desliga as bobinas e começa a contar a pausa.
void pararGiro() {
  if (!girando) return;                      // já está parado: ignora
  girando = false;
  desligarBobinas();
  inicioEstado = millis();                   // a pausa começa a contar agora
  Serial.println(F("[MOTOR] Parado."));
}

// ================================================================
//  COMUNICAÇÃO SERIAL
// ================================================================

// Mostra a lista de comandos e o estado atual no Monitor Serial.
// F("...") guarda o texto na memória flash, economizando RAM.
void mostrarAjuda() {
  Serial.println(F("--- PetFeeder ---"));
  Serial.println(F("g = girar agora | s = parar"));
  Serial.println(F("a = automatico ON | m = automatico OFF"));
  Serial.println(F("i = inverter sentido | t = anti-travamento"));
  Serial.println(F("? = ajuda"));
  Serial.print(F("Automatico: "));      Serial.println(automatico ? F("ON") : F("OFF"));
  Serial.print(F("Anti-travamento: ")); Serial.println(antiTravamento ? F("ON") : F("OFF"));
  Serial.print(F("Sentido: "));         Serial.println(sentido > 0 ? F("normal") : F("invertido"));
  Serial.print(F("Motor: "));           Serial.println(girando ? F("girando") : F("parado"));
}

// Lê todos os caracteres recebidos pela USB e executa os comandos.
// Aceita maiúsculas e minúsculas; ignora Enter (\n, \r) e letras
// desconhecidas.
void lerSerial() {
  while (Serial.available() > 0) {
    char c = tolower(Serial.read());
    switch (c) {
      case 'g':                              // girar agora
        iniciarGiro();
        break;

      case 's':                              // parar agora
        pararGiro();
        break;

      case 'a':                              // liga o modo automático
        automatico = true;
        inicioEstado = millis();             // espera uma pausa inteira antes do próximo giro
        Serial.println(F("[MODO] Automatico ON"));
        break;

      case 'm':                              // desliga o automático (só comandos manuais)
        automatico = false;
        Serial.println(F("[MODO] Automatico OFF (manual)"));
        break;

      case 'i':                              // inverte o sentido de rotação
        sentido = -sentido;
        Serial.println(F("[MOTOR] Sentido invertido"));
        break;

      case 't':                              // liga/desliga o anti-travamento
        antiTravamento = !antiTravamento;
        recuando = false;                    // volta a avançar
        inicioFase = millis();
        Serial.print(F("[MOTOR] Anti-travamento "));
        Serial.println(antiTravamento ? F("ON") : F("OFF"));
        break;

      case '?':                              // ajuda e status
        mostrarAjuda();
        break;

      default:                               // \n, \r e caracteres desconhecidos
        break;
    }
  }
}

// ================================================================
//  SETUP - executa uma vez ao ligar ou reiniciar o Arduino
// ================================================================
void setup() {
  Serial.begin(9600);                        // mesma velocidade do Monitor Serial
  motor.setSpeed(VELOCIDADE_RPM);            // define o intervalo entre passos
  desligarBobinas();                         // começa com o motor sem corrente
  inicioEstado = millis();                   // primeira pausa começa agora
  mostrarAjuda();
}

// ================================================================
//  LOOP - executa continuamente
//
//  Máquina de estados baseada em millis() (sem delay()):
//    PARADO  --(pausa terminou e automático ON, ou comando g)--> GIRANDO
//    GIRANDO --(TEMPO_GIRANDO terminou, ou comando s)----------> PARADO
//  Dentro de GIRANDO, com anti-travamento, alterna AVANÇO <-> RECUO.
//
//  As comparações usam (millis() - inicio >= duracao), que continua
//  correta quando o millis() reinicia (a cada ~49 dias).
// ================================================================
void loop() {
  lerSerial();                               // verifica comandos a cada volta do loop

  if (girando) {
    // Um único passo por volta do loop. Cada passo leva ~5 ms a 60 RPM
    // (60 s / (60 RPM × 200 passos)), então os comandos continuam
    // sendo lidos quase instantaneamente.
    motor.step(recuando ? -sentido : sentido);

    unsigned long agora = millis();

    // Anti-travamento: troca entre avanço e recuo quando a fase termina
    if (antiTravamento) {
      unsigned long duracaoFase = recuando ? TEMPO_RECUO : TEMPO_AVANCO;
      if (agora - inicioFase >= duracaoFase) {
        recuando = !recuando;
        inicioFase = agora;
      }
    }

    // Fim do giro
    if (agora - inicioEstado >= TEMPO_GIRANDO) pararGiro();

  } else if (automatico && millis() - inicioEstado >= TEMPO_PARADO) {
    // Modo automático: a pausa terminou, começa um novo giro
    iniciarGiro();
  }
}
