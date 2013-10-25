/*

 Software para automa��o residencial

Bibliotecas usadas:
Ethershield
Rele
EEPROM

Hardware:
Arduino
placa com oito reles, mas pode ser menos ou mais


 (C) Copyright 2013 teixeiraa@gmail.com

 */

#include "EtherShield.h"
#include "Arduino.h"
#include "EEPROM.h"
#include "Rele.h"

#define CMD_LEN    100   // time sync to PC is HEADER followed by unix time_t as ten ascii digits
#define HEADER     '*'   // Header tag for serial time sync message
#define TRAILER    '#'   // Trailer tag for serial time sync message
#define SEP_1      '+'   // Separador de parametros da linha de comando
#define SEP_2      '&'   // Separador de parametros da linha de comando
#define SNH_MASTER "0000"
#define BUFFER_SIZE 550
#define STR_BUFFER_SIZE 22

#define PROMPT    "Automacao residencial v0.1 - Arduino\n"

typedef struct
{
  char HDR   [ 8 + 1];
  char SNH   [ 4 + 1];
  char IP    [15 + 1]; // nnn.nnn.nnn.nnn
  char PORT  [ 5 + 1]; // nnnnn
  char MASK  [15 + 1]; // nnn.nnn.nnn.nnn
  char GW    [15 + 1]; // nnn.nnn.nnn.nnn
  char TRL   [ 8 + 1];
} CNF, *P_CNF;

static CNF Configuracao;
static uint8_t buf[BUFFER_SIZE + 1];
static char strbuf[STR_BUFFER_SIZE+1];
EtherShield es = EtherShield();

char cmd[CMD_LEN + 1];
char params[CMD_LEN + 1];
int idx = 0;

// Obj Rele
int NroReles = 8;
byte RelesPins[8] = {2, 3, 4, 5, 6, 7, 8, 9}; // Pinos do rele shield
Rele customReles = Rele(RelesPins, NroReles);  
int iTimer = 0, nTimerRele;
long nTempo = 0L;

/*  Thanks to John Byrns, who included this code in the
    XMODEM/YMODEM PROTOCOL REFERENCE, edited by Chuck Forsberg.
*/
int CalcularCRC(char *ptr, int count)
{
  int crc, i;

  crc = 0;
  while (--count >= 0)
  {
    crc = crc ^ (int)*ptr++ << 8;
    for (i = 0; i < 8; ++i)
      if (crc & 0x8000)
        crc = crc << 1 ^ 0x1021;
      else
        crc = crc << 1;
  }
  return (crc & 0xFFFF);
}

void Dump(char *buffer, int tam)
{
  int i, j;
  char *p;
  char linha [80];
  char val [3];

  p = buffer;

  while ((p - buffer) < tam)
  {
    memset (linha, ' ', 80);
    linha [79] = 0;
    linha [23] = '-';
    i = (tam - (p - buffer));
    for (j = 0; j < min (16, i); j++)
    {
      sprintf (val, "%02x", *p);
      memcpy (&linha [3*j], val, 2);
      if ((*p >= 32) && (*p <= 125))
        linha [j + 60] = *p;
      else
        linha [j + 60] = '.';
      p++;
    }
    Serial.println (linha);
  }
}

/* Converte dia/mes/ano para dia juliano */
long Data2Juliana(long dia, long mes, long ano)
{
  long mm, yy, dj;

  mm = mes - 2;
  yy = ano;
  if( mm < 1 )
  {
    mm += 12;
    yy--;
  }
  dj = ((long)(yy*1461.0/4.0)) + ((long) (mm*367.0/12.0)) + dia + 1721074L;
  dj = (dj - ((long) ( (((long) (yy/100.0))+1)*3.0/4.0)))+15;
  return dj;
}

// Converte dia juliano para dia/mes/ano
void Juliana2Data(long dj, long *dia, long *mes, long *ano)
{
  long jd2;

  jd2  = dj;
  dj   = jd2 + 30;
  *mes = ( (long) ( ( dj - 1721075.0 ) / 365.2425 * 12.0 ) ); /* meses desde 16 Jan 0 (AD) */
  *ano = ( (long) *mes / 12.0 );
  *mes = ( *mes - 12 * (*ano) ) + 1;
  *dia = 0;
  *dia = jd2 - Data2Juliana( *dia, *mes, *ano );
  dj   = jd2;
  if ( *dia > 0 ) return;
  dj   = jd2;
  *mes = ( (long) ( ( dj - 1721075.0 ) / 365.2425 * 12.0 ) ); /* meses desde 16 Jan 0 (AD) */
  *ano = ( (long) *mes / 12.0 );
  *mes = ( *mes - 12 * (*ano) ) + 1;
  *dia = 0;
  *dia = jd2 - Data2Juliana( *dia, *mes, *ano );
  dj   = jd2;
  return;
}
/* Calcula a data n dias antes ou depois de um dia/mes/ano */
//CalculaData( 1, &dia_log, &mes_log, &ano_log ); // Incrementa a data
//CalculaData( -1, &dia_log, &mes_log, &ano_log ); // decrementa a data
void CalculaData(long n_dias, long *dia, long *mes, long *ano)
{
  Juliana2Data( Data2Juliana( *dia, *mes, *ano ) + n_dias , dia, mes, ano ) ;
}
void Codifica (char *texto, int tamanho)
{
  char c;

  c = 0;
  while (tamanho--)
  {
    *texto = ((*texto ^ 53) + 41) ^ c;
    c = *texto;
    texto++;
  }
}

void Decodifica (char *texto, int tamanho)
{
  char c, c1;

  c = 0;
  while (tamanho--)
  {
    c1 = *texto;
    *texto = ((*texto ^ c) - 41) ^ 53;
    c = c1;
    texto++;
  }
}

void ConvZonadoBinario (char *ptr_zon, long tam_zon, char *ptr_dest)
{
  long i;

  for (i = 0; i < (tam_zon / 2L); i++)
  {
    (*ptr_zon >= 'A' && *ptr_zon <= 'F') ?
                  (*ptr_dest = (*ptr_zon - '7') << 4 ) :
                  (*ptr_dest = (*ptr_zon & 0x0f) << 4);
     ptr_zon++;
    (*ptr_zon >= 'A' && *ptr_zon <= 'F') ?
                  (*ptr_dest |= (*ptr_zon - '7')) :
                  (*ptr_dest |= (*ptr_zon & 0x0f));
    ptr_zon++;
    ptr_dest++;
  }
}

void ConvBinarioZonado (char *ptr_bin, int tam_bin, char *ptr_dest)
{
  int i;
  int caracter;

  for (i = 0; i < tam_bin; i++)
  {
    caracter = (*ptr_bin >> 4) & 0x000f;
    if (caracter >= 0 && caracter <= 9)
      *ptr_dest++ = caracter + 0x30;
    else
      *ptr_dest++ = caracter + 0x37;
                                
    caracter = *ptr_bin & 0x000f;
                                    
    if (caracter >= 0 && caracter <= 9)
      *ptr_dest++ = caracter + 0x30;
    else
      *ptr_dest++ = caracter + 0x37;
                                                        
    ptr_bin++;
  }
}

char *Parse (char *pStr)
{
  static char *pTmp, *p, pBuf [255];
  int tmStr, i;

  p = pStr;
  tmStr = strlen (p);
  pTmp = (char *)&pBuf [0];
  memset (pBuf, 0, sizeof (pBuf));
  if (tmStr)
  {
    // Remove todos espacos do inicio da string
    while (*p == ' ') p++;
    strcpy (pStr, p);

    // Pega uma palavra da cadeia (frase).
    i = 0;
    p = pStr;
    while (*(p + i) != ' ' && *(p + i) != 0 ) i++;
    memcpy (pTmp, p, i);
    *(pTmp + i) = 0;
    strcpy (pStr, p + i);
  }
  return pTmp;
}

void leEEPROM(char *pp, int tm)
{
  int address = 0;
  char *p = pp;

  while (address != tm)
  {
    *p = EEPROM.read(address++);
    p++;
  }
}

void gravaEEPROM(char *pp, int tm)
{
  int address = 0;
  char *p = pp;

  while (address != tm)
  {
    EEPROM.write(address++, *p);
    p++;
  }
}

void limpaEEPROM()
{
  for (int i = 0; i < 512; i++)
    EEPROM.write(i, 0);
}

void ResetSoftware()
{
  asm volatile ("  jmp 0");  
}  

char *BuscaParams(int tm)
{
  idx = 0;
  params[idx] = 0;

  while (idx < tm)
  {
    char c = Serial.read();
    if (c == SEP_2 || c == TRAILER)
    {
      idx = 0;
      break;   
    }
    else
    {
      params[idx++] = c;
      params[idx] = 0;
    }
  }
  return params;
}

void cmdSerial()
{
  idx = 0;
  cmd[idx] = 0;
  
  while(Serial.available())
  {
    char c = Serial.read(); 
    if (c == HEADER) 
    { 
      delay(1000);      
      while  (idx < CMD_LEN)
      {
        c = Serial.read();          
        if (c == SEP_1)
        {
          idx = 0;
          break;   
        }
        else
        {
          cmd[idx++] = c;
          cmd[idx] = 0;
        }
      }

      if (strcmp(cmd, "00")==0)
        TrataCmdTrocaSenha();
      else if (strcmp(cmd, "01")==0)
        TrataCmdParmsComunic();
      else if (strcmp(cmd, "02")==0)
        TrataCmdListaParmsComunic();        
      else if (strcmp(cmd, "RL")==0)
        TrataCmdReles();
      else if (strcmp(cmd, "XY")==0)
        TrataCmdRestaurar();
      else if (strcmp(cmd, "AT")==0)
        TrataCmdAutoTeste();
      else
        Serial.println("cmd: desconhecido");
    }  
  }   
}

int VerificaSenha(char *snh)
{
  if (memcmp(snh, Configuracao.SNH, 4) == 0)
    return true;
  return false;
}

//  *00+1234&0010&0010# - Troca senha
void TrataCmdTrocaSenha()
{
  char snh[10], snh1[10], snh2[10];
  Serial.println("cmd: 00");
  strncpy(snh, BuscaParams(9), 5);
  strncpy(snh1, BuscaParams(9), 5);
  strncpy(snh2, BuscaParams(9), 5);

  if (VerificaSenha(snh) && (memcmp(snh1, snh2, 4) == 0 && strlen(snh1) == 4 && strlen(snh1) == 4))
  {
    //leEEPROM((char *)&Configuracao, sizeof(CNF));
    strcpy(Configuracao.SNH, snh1);
    gravaEEPROM((char *)&Configuracao, sizeof(CNF));
    Serial.println("+OK");
  }
  else Serial.println("-ERR Senha errada");
}

//  *01+1234&192.168.2.99&80&255.255.255.0&192.168.2.1# - Troca params
void TrataCmdParmsComunic(void)
{
  CNF tmp;
  char snh[10];
  
  Serial.println("cmd: 01");
  strncpy(snh, BuscaParams(9), 5);
  strcpy(tmp.IP, BuscaParams(sizeof(tmp.IP)));
  strcpy(tmp.PORT, BuscaParams(sizeof(tmp.PORT)));
  strcpy(tmp.MASK, BuscaParams(sizeof(tmp.MASK)));
  strcpy(tmp.GW, BuscaParams(sizeof(tmp.GW)));

  if (VerificaSenha(snh))
  {
    //leEEPROM((char *)&Configuracao, sizeof(CNF));
    strcpy(Configuracao.IP, tmp.IP);
    strcpy(Configuracao.PORT, tmp.PORT);
    strcpy(Configuracao.MASK, tmp.MASK);
    strcpy(Configuracao.GW, tmp.GW);
    gravaEEPROM((char *)&Configuracao, sizeof(CNF));
    Serial.println("+OK");
    delay(1000);
    ResetSoftware();
  }
  else Serial.println("-ERR Senha errada");
}

//  *02+1234# - Lista params
void TrataCmdListaParmsComunic(void)
{
  char snh[10];
  
  Serial.println("cmd: 02");
  strncpy(snh, BuscaParams(9), 5);

  if (VerificaSenha(snh))
  {
    Serial.println("+OK");
    //leEEPROM((char *)&Configuracao, sizeof(CNF));
    Serial.print("End.IP...: ");   Serial.println(Configuracao.IP);
    Serial.print("Port.Tcp.: ");   Serial.println(Configuracao.PORT);
    Serial.print("Mascara..: ");   Serial.println(Configuracao.MASK);
    Serial.print("End.GW...: "); Serial.println(Configuracao.GW);
  }
  else Serial.println("-ERR Senha errada");
}

// *RL+0000&<SETOR|TODOS>&<ACAO>&[PARAMS]#
/*      999 = TODOS

        *RL+0000&000&LIGA#
        *RL+0000&001&DESL#
        *RL+0000&001&INVERTE#
        *RL+0000&999&ESTADO#
        *RL+0000&002&TIMER#
 */
void TrataCmdReles(void)
{
  char snh[10], setor[3+1], acao[3+1], params[10+1];
  
  Serial.println("cmd: RL");
  strncpy(snh, BuscaParams(9), 5);
  strcpy(setor, BuscaParams(sizeof(setor)));
  strcpy(acao, BuscaParams(sizeof(acao)));
  strcpy(params, BuscaParams(sizeof(params)));

  if (VerificaSenha(snh))
  {
    
    Serial.println("+OK");
  }
}

void TrataCmdRestaurar(void)
{
  char snh[10], params[100];
  
  Serial.println("cmd: XY");
  strncpy(snh, BuscaParams(9), 5);
  strcpy(params, BuscaParams(sizeof(params)));
  
  if (strcmp(snh, "N1C0") == 0 && strcmp(params, "T3X") == 0)
  {
    strcpy(Configuracao.HDR, "t31x31r4");
    gravaEEPROM((char *)&Configuracao, sizeof(CNF));
    Serial.println("+OK");
    delay(1000);
    ResetSoftware();
  }
  else Serial.println("-ERR Senha errada");
}

//  *AT+1234# - AutoTeste
void TrataCmdAutoTeste(void)
{
  char snh[10];
  int i, EstadoPino[8];
  
  Serial.println("cmd: AT");
  strncpy(snh, BuscaParams(9), 5);

  if (VerificaSenha(snh))
  {
    Serial.println("+OK");
    Serial.println("\n[auto-teste]");

    for (i=0; i<NroReles; i++)
    {
      EstadoPino[i] = customReles.Estado(i);
      customReles.Desliga(i);
    }

    for (i=0; i<NroReles; i++)
    {
      Serial.print(RelesPins[i]);
      Serial.print(" ");
      customReles.Liga(i);
      delay(100);
    }

    for (i=NroReles; i>=0; i--)
    {
      customReles.Desliga(i);
      delay(100);
    }

    for (i=0; i<NroReles; i++)
    {
      customReles.Liga(i);
      delay(40);
    }
    
    for (i=0; i<NroReles; i++)
    {
      customReles.Desliga(i);
      delay(40);
    }
    
    for (i=0; i<NroReles; i++)
      if (EstadoPino[i] == 1) customReles.Liga(i);

    Serial.println("\n.");
  }
  else Serial.println("-ERR Senha errada");
}


/*
  Formato de comando de configuração via Serial:
  *<CMD>+<Senha>&<param1>&<param2>&<param3>#

  Troca senha: *00+<Senha>&<NovaSenha>&<NovaSenha>#
  *00+1234&0011&0011#

  Troca params de comunicação: *01+<Senha>&<IP>&<PORT>&<MASK>&<GW>#
  *01+1234&192.168.2.138&80&255.255.255.0&192.168.2.1#
  
  Lista params de comunicação: *02+<Senha>#
  *02+1234#

  Retorno:  +OK ou -ERR 
-------------------------------------------------------------------
  
  Formato de comando via TCP/IP:

  <CMD><espaço><Setor><espaço>\n
  Exemplos:
    LIGA 000      --> Liga o setor 0
    DESL 999      --> Desliga todos setores
    INVERTE 001   --> Inverte o setor ou todos 999
    ESTADO        --> Recebe uma cadeia de ZEROS e UNS com o estado dos setores
    TIMER 001 60  --> Liga o Setor 1 por aproximadamente 60 segundos 
 */

void setup()
{
  int i;
  char *p;
  uint8_t oct;
  uint8_t MeuIP[4] = {0, 0, 0, 0};
  uint8_t MeuMAC[6] = {0x54, 0x55, 0x58, 0x10, 0x00, 0x25}; 

  Serial.begin(9600);

  leEEPROM((char *)&Configuracao, sizeof(CNF));
  if (strcmp(Configuracao.HDR, "T31X31R4") != 0 || 
      strcmp(Configuracao.TRL, "T31X31R4") != 0)
  {
    Serial.println("Reiniciando...");
    strcpy(Configuracao.SNH, SNH_MASTER);
    strcpy(Configuracao.IP,   "192.168.3.250");
    strcpy(Configuracao.PORT, "80");
    strcpy(Configuracao.MASK, "255.255.255.0");
    strcpy(Configuracao.GW,   "192.168.3.1");
    strcpy(Configuracao.HDR,  "T31X31R4");
    strcpy(Configuracao.TRL, "T31X31R4");
    gravaEEPROM((char *)&Configuracao, sizeof(CNF));
  }
  
  // Inicializa SPI interface
  es.ES_enc28j60SpiInit();

  // Inicializa enc28j60
  es.ES_enc28j60Init(MeuMAC);

  Serial.println(PROMPT);

  Serial.print("IP: ");
  p = Configuracao.IP; oct = atoi(p);
  Serial.print(oct); Serial.print(".");
  MeuIP[0]  = oct;

  if (oct < 10) p += 2;
  else if (oct > 9 && oct < 100) p += 3;
  else p += 4;

  oct = atoi(p);    
  Serial.print(oct); Serial.print(".");
  MeuIP[1]  = oct;

  if (oct < 10) p += 2;
  else if (oct > 9 && oct < 100) p += 3;
  else p += 4;

  oct = atoi(p);    
  Serial.print(oct); Serial.print("."); 
  MeuIP[2]  = oct;

  if (oct < 10) p += 2;
  else if (oct > 9 && oct < 100) p += 3;
  else p += 4;

  oct = atoi(p);    
  Serial.print(oct);
  MeuIP[3]  = oct;
  Serial.print(":");
  Serial.println(Configuracao.PORT);

  // Inicializa camada IP
  es.ES_init_ip_arp_udp_tcp(MeuMAC, MeuIP, atoi(Configuracao.PORT));

  Serial.println(" ");
  Serial.print(NroReles);
  Serial.print(" pinos em operacao [");

  for (i=0; i<NroReles; i++)
  {
    Serial.print(RelesPins[i]);
    if (i<NroReles-1) Serial.print(", ");
    delay(100);
  }
  Serial.println("]\n\nOK");
}

void loop()
{
  uint16_t tmTX, RX;
  int nRele;
  char Stt[20], Cmd[20], Params[20];
  char *p, *pp, *ppp;

  while(1)
  {
    if (Serial.available()) cmdSerial();

    if (iTimer == 1)
    {
      if (nTempo-- == 0L)
      {
        iTimer = 0;
        customReles.Desliga(nTimerRele);
      }
    }

    memset(buf, 0, BUFFER_SIZE);
    if((RX=es.ES_packetloop_icmp_tcp(buf, es.ES_enc28j60PacketReceive(BUFFER_SIZE, buf))) == 0)
      continue;
       
    p = pp = (char *)&(buf[RX]);
    if (strlen(ppp = Parse (pp)) != 0)
    {
      Serial.print("CMD:"); // debug
      Serial.println(ppp); // debug
      strcpy(Cmd, ppp);
    }
    
    if (strlen(ppp = Parse (pp)) != 0)
    {
      Serial.print("RELE:"); // debug
      Serial.println(ppp); // debug
      strcpy(Params, ppp);
      nRele = atoi(Params);
      if (nRele != 999 && (nRele < 0 || nRele >= NroReles))
      {
        tmTX=es.ES_fill_tcp_data_p(buf, 0, PSTR("-ERR 002 "));
        es.ES_www_server_reply(buf, tmTX);
        continue;
      }      
    }

    if (strcmp(Cmd, "TIMER") == 0 && iTimer == 0)
    {
      if (strlen(ppp = Parse (pp)) != 0)
      {
        strcpy(Params, ppp);
        nTempo = atol(Params);        
        if (nTempo < 0L || nTempo > 60L)
        {
          tmTX=es.ES_fill_tcp_data_p(buf, 0, PSTR("-ERR 003 "));
          es.ES_www_server_reply(buf, tmTX);
          continue;
        }
        nTempo = nTempo * 300000L;      
      }
      iTimer = 1;
      nTimerRele = nRele;
      customReles.Liga(nTimerRele);
    }
    else if (strcmp(Cmd, "LIGA") == 0)
    {
      if (nRele == 999)
        for (nRele=0; nRele<NroReles; nRele++)
          customReles.Liga(nRele);
      else
        customReles.Liga(nRele);
    }
    else if (strcmp(Cmd, "DESL") == 0)
    {
      if (nRele == 999)
        for (nRele=0; nRele<NroReles; nRele++)
          customReles.Desliga(nRele);
      else
        customReles.Desliga(nRele);
    }
    else if (strcmp(Cmd, "INVERTE") == 0)
    {
      if (nRele == 999)
      {
        for (nRele=0; nRele<NroReles; nRele++)
        {
          if (customReles.Estado(nRele) == 0)
            customReles.Liga(nRele);
          else
            customReles.Desliga(nRele);
        }
      }
      else
        if (customReles.Estado(nRele) == 0)
          customReles.Liga(nRele);
        else
          customReles.Desliga(nRele);
    }
    else if (strcmp(Cmd, "ESTADO") == 0)
    {
      
    }
    else
    {  
      tmTX=es.ES_fill_tcp_data_p(buf, 0, PSTR("-ERR 001 "));
      es.ES_www_server_reply(buf, tmTX);
      continue;
    }

    tmTX=es.ES_fill_tcp_data_p(buf, 0, PSTR("+OK "));
    tmTX=es.ES_fill_tcp_data_p(buf, tmTX, PSTR("RELES:"));
    sprintf(Stt, "%02d:", NroReles);
    tmTX=es.ES_fill_tcp_data_len(buf, tmTX, (char *)Stt, strlen(Stt));

    memset(Stt, 0, sizeof(Stt));
    for (nRele=0; nRele<NroReles; nRele++)
    {
      sprintf(strbuf, "%d", customReles.Estado(nRele));
      strcat(Stt, strbuf);
    }
    tmTX=es.ES_fill_tcp_data_len(buf, tmTX, (char *)Stt, strlen(Stt));
    es.ES_www_server_reply(buf, tmTX);
  } // while
}


