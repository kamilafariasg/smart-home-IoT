#include <WiFiClientSecure.h> //para o pushbullet
#include <SPI.h> //biblioteca para o RFID
#include <MFRC522.h> // biblioteca para o RFID
#include <ESP8266WiFi.h> //para a conexao com o wifi
#include <BlynkSimpleEsp8266.h> // biblioteca para usar o serviço Blynk
#include<DHT.h> //biblioteca pro sensor de umidade/temperatura
#include "Thread.h" //para auxiliar nos processos "paralelos" (Threads)
#include "ThreadController.h" //agrupar e controlar as threads 
#include <PubSubClient.h> // Biblioteca usada pelo mqtt

#define PIN_MOVIMENTO  D0  //sensor de movimento
#define SS_PIN 4  //D2 //RFID
#define RST_PIN 5 //D1 //RFID
#define BLYNK_PRINT Serial //Exibe no monitor serial que o node se conectou ao blynk
#define DHTPIN  D4 //pino d8 onde ficou a leitura do sensor

WiFiClient client;//cliente para o think speak
WiFiClient client_2; //cliente para o mqtt
MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance. //RFID
DHT dht(DHTPIN, DHT11); //temp e umidade

const char* ssid = "Ediseuda"; //ssid do wifi
const char* password = "semsenha"; //senha do wifi

const char* server = "api.thingspeak.com"; //servidor do thinkspeak
const char* host = "api.pushbullet.com"; //servidor do pushbullet
const int httpsPort = 443;

const char* mqttServer = "m12.cloudmqtt.com"; // Aqui o endereço do servidor fornecido pelo site
const int mqttPort = 15619; // Aqui eh a porta fornecida pelo site
const char* mqttUser = "llqsihfm"; //  Aqui o nome de usuario fornecido pelo site
const char* mqttPassword = "b9AfPsyr9Jb_"; //  Aqui a senha fornecida pelo site

PubSubClient client_mqtt(client_2); //MQTT
void callback(char* topic, byte* payload, unsigned int length); //MQTT

const char* PushBulletAPIKEY = "o.JxTon32hRv2dlwjc2xZOzdqdjSEPqZXj"; //chave do pushbullet
String apiKey = "XWWITHEQOPOG7T99";     //  chave da API do ThingSpeak
char auth[] = "9f7247861f6f40798ab2c7a6873d72ff"; //chave de autenticacao do projeto do blynk

float t = 0; //var onde fica a temperatura
float h = 50; //var onde fica a umidade, inicio com 50 para nao enviar a primeira not de baixa umidade, na primeira chamada esse valor eh mudado
boolean val_sensor_mov = 0; //variavel vai guardar o valor do sensor de movimento

/////////////////////////////Threads

Thread threadDoBlynk;
void func_Blynk() {
  Blynk.run(); //chama run do Blynk que já faz tudo para o aplicativo no smartphone
}

Thread threadDoMQTT;
void func_MQTT() {
  client_mqtt.loop(); //fica ouvindo o servidor mqtt
}

Thread threadDaTemperatura_Umidade;
void temp_umi() {
  //se retornar nan ele continua com a temp e a umidade anterior
  //da NaN quando da mal contato no sensor
  if (!isnan(dht.readTemperature())) {
    t = dht.readTemperature(); //leitura da temperatura
  }
  if (!isnan(dht.readHumidity())) {
    h = dht.readHumidity(); //leitura da umidade
  }
  if (t >= 55) {
    //se temperatura acima de 55 ele avisa que pode esta acontecendo um incendio
    notificar_temp_elevada();
  }
  if (h <= 30) {
    //se umidade abaixo de 30 ele avisa para o usuario ligar o umidificador de ar
    notificar_umidade_baixa();
  }
}

int valor_mov_thinkspeak = 0; //serve para guardar o valor do movimento para enviar para o ThinkSpeak
int aux = 0; //auxilia o ThinkSpeak

Thread threadDoMovimento;
void movimento() {
  val_sensor_mov = digitalRead(PIN_MOVIMENTO);

  if (aux == 0) { //serve para o thinkspeak
    valor_mov_thinkspeak = 0;
  }

  if (val_sensor_mov == 1) {
    notificar_sensor_movimento(); //chama uma notificacao se tiver movimento

    valor_mov_thinkspeak = 1; //o que vai para o thinkspeak
    aux = aux + 1;
  }
}

int valor_rfid_thinkspeak = 0; //serve para guardar o valor do rfid para enviar para o ThinkSpeak
int aux2 = 0; //auxilia o ThinkSpeak

//RFID: 
//Cartão RU Hugo: 34 37 38 05
//Cartão RU Thaís: 84 8E D5 10
//Cartão Branco: 50 CA 19 A8
//Chaveiro: D5 3C B1 79

Thread threadDoRFID;
void func_RFID() {
  if (aux2 == 0) { //serve para o thinkspeak
    valor_rfid_thinkspeak = 0;
  }
  // Look for new cards
  if ( mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {

    valor_rfid_thinkspeak = 1; //o que vai para o thinkspeak
    aux2 = aux2 + 1;

    //Show UID on serial monitor
    Serial.println();
    Serial.print(" RFID tag :");
    String content = "";
    byte letter;
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(mfrc522.uid.uidByte[i], HEX);
      content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
      content.concat(String(mfrc522.uid.uidByte[i], HEX));
    }
    content.toUpperCase();
    Serial.println();
    if (content.substring(1) == "84 8E D5 10") { //change UID of the card that you want to give access
      Serial.println(" Acesso Autorizado ");
      Serial.println(" Bem-vindo Thais ");
      notificar_thais_rfid(); //chama notificação
    }
    else if (content.substring(1) == "D5 3C B1 79") { //change UID of the card that you want to give access
      Serial.println(" Acesso Autorizado ");
      Serial.println(" Bem-vindo Senhor Chaveiro ");
      notificar_chav_rfid(); //chama notificação
    }
    else {
      Serial.println(" Acesso Negado ");
      notificar_intruso();
    }
  }
}


Thread threadDoThinkSpeak;
void enviar_T_S() {
  aux = !aux;
  aux2 = !aux2;
  //se houver algum movimento dentro dos 20 segundos manda pro site com o valor 1
  // da mesma forma se houver alguma leitura de rfid, envia pro site

  if (client.connect(server, 80)) { //   "184.106.153.149" or api.thingspeak.com

    String postStr = apiKey;
    postStr += "&field1="; //<-- atenção, esse é o campo 1 que você escolheu no canal do ThingSpeak //temperatura
    postStr += String(t);
    postStr += "&field2="; //umidade
    postStr += String(h);
    postStr += "&field3="; //movimento
    postStr += String(valor_mov_thinkspeak);
    postStr += "&field4="; //rfid
    postStr += String(valor_rfid_thinkspeak);

    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + apiKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);

    Serial.println("Enviando dados para o ThingSpeak");
    Serial.println();
  }
  client.stop();
}

int sn_temp = 1; //inicia ativado
BLYNK_WRITE(2) {
  sn_temp = param.asInt(); //ler o botao do app
}
int sn_movimento = 1; //inicia ativado
BLYNK_WRITE(3) {
  sn_movimento = param.asInt(); //ler o botao do app
}
int sn_rfid = 1; //inicia ativado
BLYNK_WRITE(4) {
  sn_rfid = param.asInt(); //ler o botao do app
}

Thread threadAtivarDesativar;
void ativa_desativa() {
  //desativa ou ativa as threads de acordo com os botoes do blynk
  if (sn_temp == 1) {
    threadDaTemperatura_Umidade.enabled = true; //ativa
  } else {
    threadDaTemperatura_Umidade.enabled = false; //desativa
    t = 0;
    h = 0;
  }
  if (sn_movimento == 1) {
    threadDoMovimento.enabled = true;
  } else {
    threadDoMovimento.enabled = false;
    valor_mov_thinkspeak = 0;
  }
  if (sn_rfid == 1) {
    threadDoRFID.enabled = true;
  } else {
    threadDoRFID.enabled = false;
    valor_rfid_thinkspeak = 0;
  }
}


Thread threadDoEscrever;
void escreve() {
  // essa funcao escreve nas portas virtuais V0 e V1 os valores de temperatura e umidade,
  // o blynk esta lendo essas portas virtuais

  Blynk.virtualWrite(0, ((int)t));
  Blynk.virtualWrite(1, ((int)h));
}

ThreadController Todas_as_Threads; //controler que vai juntar todas as threads

///////////////////////// notificacoes

void notificar_inicio() {
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;

  if (!client.connect(host, httpsPort)) {
    Serial.println("Conexao Falhou");
    return;
  }

  String url = "/v2/pushes";
  String messagebody = "{\"type\": \"note\", \"title\": \"Projeto Final IoT\", \"body\": \"Sistema Iniciado!\"}\r\n";

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + PushBulletAPIKEY + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " +
               String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);
  Serial.println();
  Serial.println("Notificacao de inicio de sistema enviada. ");

}

void notificar_chav_rfid() {
  WiFiClientSecure client;

  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

  String url = "/v2/pushes";
  String messagebody = "{\"type\": \"note\", \"title\": \"Projeto Final IoT\", \"body\": \"Chaveiro Identificado!\"}\r\n";

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + PushBulletAPIKEY + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " +
               String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);
  Serial.println();
  Serial.println("Notificacao do Chaveiro enviada");
}
void notificar_thais_rfid() {
  WiFiClientSecure client;

  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

  String url = "/v2/pushes";
  String messagebody = "{\"type\": \"note\", \"title\": \"Projeto Final IoT\", \"body\": \"Thais Identificado!\"}\r\n";

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + PushBulletAPIKEY + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " +
               String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);
  Serial.println();
  Serial.println("Notificacao de Thais enviada");
}
void notificar_intruso() {
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;

  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

  String url = "/v2/pushes";
  String messagebody = "{\"type\": \"note\", \"title\": \"Projeto Final IoT\", \"body\": \"Intruso tentando se identificar!\"}\r\n";

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + PushBulletAPIKEY + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " +
               String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);
  Serial.println();
  Serial.println("Notificacao de Intruso enviada.");

}

void notificar_sensor_movimento() {
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;

  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

  String url = "/v2/pushes";
  String messagebody = "{\"type\": \"note\", \"title\": \"Projeto Final IoT\", \"body\": \"MOVIMENTO DETECTADO!\"}\r\n";

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + PushBulletAPIKEY + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " +
               String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);
  Serial.println();
  Serial.println("Notificacao de Movimento enviada.");

}

void notificar_temp_elevada() {
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;

  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

  String url = "/v2/pushes";
  String messagebody = "{\"type\": \"note\", \"title\": \"Projeto Final IoT\", \"body\": \"Temperatura Elevada, possível incêndio!!\"}\r\n";

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + PushBulletAPIKEY + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " +
               String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);
  Serial.println();
  Serial.println("Notificacao de Temperatura elevada enviada.");

}

void notificar_umidade_baixa() {
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;

  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

  String url = "/v2/pushes";
  String messagebody = "{\"type\": \"note\", \"title\": \"Projeto Final IoT\", \"body\": \"Umidade muito baixa, se hidrate e ligue o umidificador de ar!!\"}\r\n";

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + PushBulletAPIKEY + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " +
               String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);
  Serial.println();
  Serial.println("Notificacao de umidade baixa enviada.");

}


void setup() {
  // put your setup code here, to run once:
  Blynk.begin(auth, ssid, password); //se conecta ao blynk

  Serial.begin(115200); //velocidade monitor serial

  Serial.print("Concectado a Rede WIFI: ");
  Serial.println(ssid);
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: "); Serial.println(WiFi.localIP()); //ip do nodeMCU
  Serial.print("MAC: "); Serial.println(WiFi.macAddress()); //mac do nodeMCU
  Serial.print("Gateway:"); Serial.println(WiFi.gatewayIP()); //gateway
  Serial.println();

  dht.begin(); //inicia a biblioteca da temp e umidade

  SPI.begin();      // Initiate  SPI bus //RFID
  mfrc522.PCD_Init();   // Initiate MFRC522 //RFID

  client_mqtt.setServer(mqttServer, mqttPort);
  client_mqtt.setCallback(callback);

  while (!client_mqtt.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client_mqtt.connect("ESP8266Client", mqttUser, mqttPassword )) {
      Serial.println("Conectado ao CloudMQTT");
    } else {
      Serial.print("failed with state ");
      Serial.print(client_mqtt.state());
      delay(100);
    }
  }

  client_mqtt.publish("Ligado", "Iniciado"); //publica no mqtt que esta ligado
  client_mqtt.subscribe("PROJETO"); //topico para receber as coisas do MQTT

  threadDoBlynk.setInterval(100); //tempo que aquela thread vai levar
  threadDoBlynk.onRun(func_Blynk); //e o que ela faz, a funcao que ela chama

  threadDoMQTT.setInterval(1000);
  threadDoMQTT.onRun(func_MQTT);

  threadDoThinkSpeak.setInterval(20000);   // thingspeak precisa no minimo de 15 seg de delay entre os envios, no caso usamos 20 como seguranca
  threadDoThinkSpeak.onRun(enviar_T_S);

  threadAtivarDesativar.setInterval(500);
  threadAtivarDesativar.onRun(ativa_desativa);

  threadDaTemperatura_Umidade.setInterval(1000);
  threadDaTemperatura_Umidade.onRun(temp_umi);

  threadDoMovimento.setInterval(1000);
  threadDoMovimento.onRun(movimento);

  threadDoEscrever.setInterval(2000);
  threadDoEscrever.onRun(escreve);

  threadDoRFID.setInterval(500);
  threadDoRFID.onRun(func_RFID);

  Todas_as_Threads.add(&threadDoBlynk);//adiciona as threads ao controller
  Todas_as_Threads.add(&threadDoMQTT);
  Todas_as_Threads.add(&threadDoThinkSpeak);
  Todas_as_Threads.add(&threadAtivarDesativar);
  Todas_as_Threads.add(&threadDaTemperatura_Umidade);
  Todas_as_Threads.add(&threadDoMovimento);
  Todas_as_Threads.add(&threadDoEscrever);
  Todas_as_Threads.add(&threadDoRFID);

  notificar_inicio();
  Serial.println("");
}


void callback(char* topic, byte * payload, unsigned int length) {

  Serial.print("Mensagem recebida no topico: ");
  Serial.println(topic);

  //Serial.print("Mensagem:");
  for (int i = 0; i < length; i++) {
    //Serial.print((char)payload[i]);

    String msg;

    //obtem a string do payload recebido
    for (int i = 0; i < length; i++) {
      char c = (char)payload[i];
      msg += c;
    }

    if (msg.equals("temperatura")) {
      char temperatura[5]; //publish so funciona com vetor de char,
      sprintf(temperatura, "%.2f", t); //entao eh necessario a conversao de float para vetor de char

      client_mqtt.publish("Temperatura", temperatura); //publicacao
    }
    if (msg.equals("umidade")) {
      char umidade[5];
      sprintf(umidade, "%.2f", h);

      client_mqtt.publish("Umidade", umidade);
    }
    if (msg.equals("Desliga_AT_DES")) {
      threadAtivarDesativar.enabled = false; //desativa
      
      char resultado[10] = "Desligado";
      client_mqtt.publish("Blynk", resultado);
    }
    if (msg.equals("Liga_AT_DES")) {
      threadAtivarDesativar.enabled = true; //desativa
      
      char resultado[10] = "Ligado";
      client_mqtt.publish("Blynk", resultado);
    }
    
    
    if (msg.equals("Desliga_DHT")) {
      threadDaTemperatura_Umidade.enabled = false; //desativa
      t = 0;
      h = 0;

      char resultado[10] = "Desligado";
      client_mqtt.publish("DHT", resultado);
    }
    if (msg.equals("Liga_DHT")) {
      threadDaTemperatura_Umidade.enabled = true; //ativa

      char resultado[10] = "Ligado";
      client_mqtt.publish("DHT", resultado);
    }
    if (msg.equals("Desliga_RFID")) {
      threadDoRFID.enabled = false;
      valor_rfid_thinkspeak = 0;

      char resultado[10] = "Desligado";
      client_mqtt.publish("RFID", resultado);
    }
    if (msg.equals("Liga_RFID")) {
      threadDoRFID.enabled = true;

      
      char resultado[10] = "Ligado";
      client_mqtt.publish("RFID", resultado);
    }
    if (msg.equals("Desliga_movimento")) {
      threadDoMovimento.enabled = false;
      valor_mov_thinkspeak = 0;

      
      char resultado[10] = "Desligado";
      client_mqtt.publish("Movimento", resultado);
    }
    if (msg.equals("Liga_movimento")) {
      threadDoMovimento.enabled = true;

      char resultado[10] = "Ligado";
      client_mqtt.publish("Movimento", resultado);
    }
    
  }

  //Serial.println();
  //Serial.println("-----------------------------");

}
void loop() {
  // put your main code here, to run repeatedly:
  Todas_as_Threads.run(); //confere quem deveria rodar e chama só quem deve ser chamado
}

