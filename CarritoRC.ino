#include <Arduino.h>
#include "BluetoothSerial.h"


// ============================================================================
// 1. DEFINICIÓN DE PINES GPIO Y VARIABLES GLOBALES
// ============================================================================


// ---- L298N (Motores) ----
#define IN1 26
#define IN2 27
#define IN3 14
#define IN4 12
#define ENA 25
#define ENB 13


// ---- Periféricos y Sensores ----
const int PIN_BUZZER = 33;
const int PIN_TRIG = 32;
const int PIN_ECHO = 35;
const int PIN_SEGUIDOR_1 = 23; // Izquierdo
const int PIN_SEGUIDOR_2 = 15; // Derecho


// ---- Iluminación RGB ----
const int PIN_FRONT_LED_R_1 = 5;
const int PIN_FRONT_LED_G_1 = 18;
const int PIN_FRONT_LED_B_1 = 19;
const int PIN_FRONT_LED_R_2 = 4;
const int PIN_FRONT_LED_G_2 = 16;
const int PIN_FRONT_LED_B_2 = 17;
const int PIN_REAR_LED_R = 22;
const int PIN_REAR_LED_G = 21;


// ---- Variables de Estado ----
int velocidadActual = 127;
int velocidadManualUsuario = 127;
int estadoMovimiento = 0; // 0: Parado, 1: Adelante, 2: Atrás, 3: Giro
bool modoSeguidorActivo = false;
bool activarParpadeoTrasero = false;


// ---- Colores RGB Dinámicos ----
int estadoR = 255;
int estadoG = 255;
int estadoB = 110;
int estadoRearR = 250;
int estadoRearG = 100;


// ---- Tiempos y Failsafe ----
unsigned long previoMillisTrasero = 0;
const long intervaloParpadeo = 300;
bool estadoLedTrasero = false;
static unsigned long ultimaComunicacionBT = 0;
const unsigned long TIMEOUT_SEGURIDAD_BT = 450;


BluetoothSerial SerialBT;


// ============================================================================
// 2. FUNCIONES DE CONTROL DE MOTORES
// ============================================================================


void setupMotores()
{
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);
    pinMode(ENA, OUTPUT);
    pinMode(ENB, OUTPUT);
    delay(10);
    analogWrite(ENA, velocidadActual);
    analogWrite(ENB, velocidadActual);
}


void actualizarVelocidad(int nuevaVel)
{
    velocidadActual = constrain(nuevaVel, 0, 255);
    analogWrite(ENA, velocidadActual);
    analogWrite(ENB, velocidadActual);
}


void adelante()
{
    estadoMovimiento = 1;
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
}


void atras()
{
    estadoMovimiento = 2;
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
}


void izquierda()
{
    estadoMovimiento = 3;
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
}


void derecha()
{
    estadoMovimiento = 3;
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
}


void parar()
{
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
}


void giroSuaveDerecha()
{
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, 110);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
    analogWrite(ENB, 100);
}


void giroSuaveIzquierda()
{
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    analogWrite(ENA, 100);
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
    analogWrite(ENB, 110);
}


// ============================================================================
// 3. FUNCIONES DE COMPONENTES Y SENSORES
// ============================================================================


void setupBuzzer()
{
    pinMode(PIN_BUZZER, OUTPUT);
    noTone(PIN_BUZZER);
}


void controlarClaxon(bool encender)
{
    if (encender)
        tone(PIN_BUZZER, 2000);
    else
    {
        noTone(PIN_BUZZER);
        digitalWrite(PIN_BUZZER, LOW);
    }
}


void setupSensorUltrasonico()
{
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
}


float obtenerDistancia()
{
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    long duracion = pulseIn(PIN_ECHO, HIGH, 30000);
    if (duracion == 0)
        return 999.0;
    return (duracion * 0.0343) / 2.0;
}


void verificarObstaculos()
{
    float distancia = obtenerDistancia();
    float distanciaFrenado = 10.0 + (velocidadActual * 0.07);
    if (distancia <= distanciaFrenado && distancia > 2.0 && estadoMovimiento == 1)
    {
        parar();
        Serial.println("¡ALERTA!: Obstáculo al frente. Frenado automático.");
    }
}


void setupSensoresLinea()
{
    pinMode(PIN_SEGUIDOR_1, INPUT);
    pinMode(PIN_SEGUIDOR_2, INPUT);
}


int leerSensoresLinea()
{
    int e1 = digitalRead(PIN_SEGUIDOR_1);
    int e2 = digitalRead(PIN_SEGUIDOR_2);


    if (e1 == LOW && e2 == LOW)
    {
        actualizarVelocidad(92);
        adelante();
        return 0;
    }
    else if (e1 == LOW && e2 == HIGH)
    {
        giroSuaveDerecha();
        delay(80);
        return 1;
    }
    else if (e1 == HIGH && e2 == LOW)
    {
        giroSuaveIzquierda();
        delay(80);
        return 2;
    }
    else
    {
        parar();
        return 3;
    }
}


void ejecutarSeguidorLinea()
{
    if (modoSeguidorActivo)
        leerSensoresLinea();
}


void setupFrontLed()
{
    pinMode(PIN_FRONT_LED_R_1, OUTPUT);
    pinMode(PIN_FRONT_LED_G_1, OUTPUT);
    pinMode(PIN_FRONT_LED_B_1, OUTPUT);
    pinMode(PIN_FRONT_LED_R_2, OUTPUT);
    pinMode(PIN_FRONT_LED_G_2, OUTPUT);
    pinMode(PIN_FRONT_LED_B_2, OUTPUT);
}


void actualizarColorDinamico(int r, int g, int b)
{
    estadoR = r;
    estadoG = g;
    estadoB = b;
}


void controladorLucesDelanteras(bool encendido)
{
    if (encendido)
    {
        analogWrite(PIN_FRONT_LED_R_1, estadoR);
        analogWrite(PIN_FRONT_LED_G_1, estadoG);
        analogWrite(PIN_FRONT_LED_B_1, estadoB);
        analogWrite(PIN_FRONT_LED_R_2, estadoR);
        analogWrite(PIN_FRONT_LED_G_2, estadoG);
        analogWrite(PIN_FRONT_LED_B_2, estadoB);
    }
    else
    {
        analogWrite(PIN_FRONT_LED_R_1, 0);
        analogWrite(PIN_FRONT_LED_G_1, 0);
        analogWrite(PIN_FRONT_LED_B_1, 0);
        analogWrite(PIN_FRONT_LED_R_2, 0);
        analogWrite(PIN_FRONT_LED_G_2, 0);
        analogWrite(PIN_FRONT_LED_B_2, 0);
    }
}


void setupRearLed()
{
    pinMode(PIN_REAR_LED_R, OUTPUT);
    pinMode(PIN_REAR_LED_G, OUTPUT);
}


void actualizarColorDinamicoTrasero(int r, int g)
{
    estadoRearR = r;
    estadoRearG = g;
}


void controladorLucesTraseras(bool encendido)
{
    if (encendido)
    {
        unsigned long actualMillis = millis();
        if (actualMillis - previoMillisTrasero >= intervaloParpadeo)
        {
            previoMillisTrasero = actualMillis;
            estadoLedTrasero = !estadoLedTrasero;
            analogWrite(PIN_REAR_LED_R, estadoLedTrasero ? estadoRearR : 0);
            analogWrite(PIN_REAR_LED_G, estadoLedTrasero ? estadoRearG : 0);
        }
    }
    else
    {
        analogWrite(PIN_REAR_LED_R, 0);
        analogWrite(PIN_REAR_LED_G, 0);
        estadoLedTrasero = false;
    }
}


// ============================================================================
// 4. SERVICIO BLUETOOTH Y COMANDOS DE LA APP
// ============================================================================


void iniciarBluetooth() 
{ 
    if (!SerialBT.begin("RS Tech - CarBot v1.0.0")) 
    {
        Serial.println("Error al iniciar Bluetooth");
    } 
    else 
    {
        Serial.println("Bluetooth iniciado con exito: MiESP32");
    }
}


void gestionarBluetooth()
{
    // Si no hay un teléfono/app conectado, detenemos los sistemas de movimiento por seguridad
    if (!SerialBT.hasClient())
    {
        modoSeguidorActivo = false;
        activarParpadeoTrasero = false;
        parar();
        controlarClaxon(false);
        controladorLucesTraseras(false);
        controladorLucesDelanteras(false);
    }

    // Solo leemos comandos si realmente hay un cliente conectado y datos disponibles
    if (SerialBT.hasClient() && SerialBT.available())
    {
        char command = SerialBT.read();
        if (command == '\n' || command == '\r')
            return;

        ultimaComunicacionBT = millis();

        switch (command)
        {
        case 'W':
            modoSeguidorActivo = true;
            break;
        case 'w':
            modoSeguidorActivo = false;
            parar();
            actualizarVelocidad(velocidadManualUsuario);
            break;
        case 'F':
            if (!modoSeguidorActivo)
                adelante();
            break;
        case 'B':
            if (!modoSeguidorActivo)
                atras();
            break;
        case 'L':
            if (!modoSeguidorActivo)
                izquierda();
            break;
        case 'R':
            if (!modoSeguidorActivo)
                derecha();
            break;
        case 'S':
            parar();
            break;
        case '1':
            velocidadManualUsuario = 127;
            actualizarVelocidad(velocidadManualUsuario);
            break;
        case '2':
            velocidadManualUsuario = 190;
            actualizarVelocidad(velocidadManualUsuario);
            break;
        case '3':
            velocidadManualUsuario = 255;
            actualizarVelocidad(velocidadManualUsuario);
            break;
        case 'C':
            controlarClaxon(true);
            break;
        case 'c':
            controlarClaxon(false);
            break;
        case 'D':
            controladorLucesDelanteras(true);
            break;
        case 'd':
            controladorLucesDelanteras(false);
            break;
        case 'X':
        {
            delay(10);
            if (SerialBT.read() == ':')
            {
                int nR = SerialBT.parseInt();
                int nG = SerialBT.parseInt();
                int nB = SerialBT.parseInt();
                actualizarColorDinamico(nR, nG, nB);
                controladorLucesDelanteras(true);
            }
        }
        break;
        case 'T':
            activarParpadeoTrasero = true;
            break;
        case 't':
            activarParpadeoTrasero = false;
            controladorLucesTraseras(false);
            break;
        case 'Y':
        {
            delay(10);
            if (SerialBT.read() == ':')
            {
                int nR = SerialBT.parseInt();
                int nG = SerialBT.parseInt();
                actualizarColorDinamicoTrasero(nR, nG);
            }
        }
        break;
        default:
            if (!modoSeguidorActivo)
                parar();
            break;
        }
    }

    if (SerialBT.hasClient())
    {
        controladorLucesTraseras(activarParpadeoTrasero);

        if (!modoSeguidorActivo && (millis() - ultimaComunicacionBT > TIMEOUT_SEGURIDAD_BT))
        {
            parar();
            controlarClaxon(false);
        }
    }
}


// ============================================================================
// 5. CONFIGURACIÓN E INICIALIZACIÓN (SETUP) Y BUCLE PRINCIPAL (LOOP)
// ============================================================================


void setup()
{
    Serial.begin(115200);
    setupMotores();
    setupBuzzer();
    setupSensorUltrasonico();
    setupSensoresLinea();
    setupFrontLed();
    setupRearLed();
    iniciarBluetooth();
    Serial.println("--- Sistema Inicializado: Vehículo Listo ---");
}


void loop()
{
    gestionarBluetooth();
    verificarObstaculos();
    if (modoSeguidorActivo)
    {
        ejecutarSeguidorLinea();
    }
    controladorLucesTraseras(activarParpadeoTrasero);
}
