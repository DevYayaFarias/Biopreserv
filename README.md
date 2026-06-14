# 🍃 BioPreserv

> **Sistema Ciberfísico Preditivo para Preservação de Acervos**  
> Monitoramento inteligente de temperatura, umidade e qualidade do ar para proteger livros e documentos históricos contra mofo — antes que o dano aconteça.

---

## 📖 Sobre o Projeto

Acervos bibliográficos e documentos históricos são patrimônios insubstituíveis. Um livro tomado pelo mofo não tem segunda chance.

O **BioPreserv** nasceu de uma pergunta simples: *e se a tecnologia pudesse avisar sobre o risco antes que ele se torne visível?*

Utilizando uma rede de sensores inteligentes embarcados em um ESP32, o sistema monitora continuamente os microclimas entre prateleiras — temperatura, umidade e qualidade do ar — cruzando esses dados com modelos de degradação biológica para emitir alertas preditivos de formação de fungos. Tudo isso em tempo real, com interface web embarcada e custo acessível.

> Desenvolvido como Trabalho Discente Efetivo (TDE) da disciplina **Performance em Sistemas Ciberfísicos** — Engenharia de Software, PUCPR, 2026/1.

---

## 🎯 Objetivo

Automatizar a conservação de livros e documentos com alerta preditivo contra mofo, monitorando continuamente temperatura, umidade e qualidade do ar em diferentes pontos das estantes. O projeto identifica riscos ambientais antes que danos físicos ocorram, cruzando dados climáticos com modelos de degradação biológica para garantir a integridade de acervos históricos e bibliográficos de forma proativa e em tempo real.

---

## 🔧 Arquitetura do Sistema

### 🧠 Processamento
| Componente | Função |
|---|---|
| **ESP32** | Microcontrolador principal com Wi-Fi integrado, responsável pela leitura dos sensores, análise contínua dos dados, gerenciamento de tarefas e hospedagem da interface web embarcada |

### 📡 Sensores (Entradas)
| Sensor | Função |
|---|---|
| **DHT22** | Leitura de temperatura e umidade relativa do ar |
| **MQ-135** | Detecção de gases e monitoramento da qualidade do ar |

### ⚙️ Atuadores (Saídas)
| Atuador | Função |
|---|---|
| **Servo Motor SG90 (180°)** | Abertura/fechamento de ventilação ou janelas de ventilação da caixa |
| **Módulo Relé 5V 1 Canal c/ Optoacoplador** | Acionamento de dispositivos externos de forma isolada e segura |
| **Mini Cooler Fan 5V 40x40mm** | Circulação de ar para controle ativo do microclima |

---

## 🌐 Interface Web Embarcada

Acesse o sistema diretamente pelo navegador, sem precisar instalar nada. A interface inclui:

- **📊 Guia de Performance** — uso de CPU, memória (heap, stack, flash), tempo de execução das funções principais, status das tasks, conexão Wi-Fi e gráficos em série temporal
- **📋 Sistema de Logs** — registros de erro, warning e informação, com exportação disponível
- **⚙️ Configurações** — frequência de leitura dos sensores, parâmetros operacionais e troca do ponto de acesso Wi-Fi sem regravação do firmware
- **ℹ️ Sobre** — informações do projeto, integrantes e link para este repositório
- **📝 Changelog** — histórico de versões acessível diretamente pela interface

---

## ✅ Requisitos Atendidos

### Funcionais
- [x] Interface web embarcada no ESP32
- [x] Guia única de performance com métricas completas
- [x] Sistema de logs com exportação
- [x] Configuração dinâmica via web (incluindo Wi-Fi)
- [x] Guia "Sobre" com dados da equipe
- [x] Changelog acessível pela interface

### Não Funcionais
- [x] Persistência de dados com histórico mínimo de 24 horas
- [x] Ausência de rotinas bloqueantes (`delay()` proibido)
- [x] Uso de múltiplas tasks (FreeRTOS)
- [x] Uso de interrupção de hardware
- [x] Gerenciamento de energia (Light Sleep / Deep Sleep)
- [x] Código versionado em repositório público no GitHub

### Hardware
- [x] Mínimo de 2 sensores de entrada (DHT22 + MQ-135)
- [x] Mínimo de 2 atuadores, não sendo LEDs nem buzzers (Servo + Relé + Cooler)
- [x] Sistema acondicionado em caixa física
- [x] Montagem organizada, segura e funcional

---

## 🗂️ Estrutura do Repositório

```
biopreserv/
├── biopreserv/
│   └── biopreserv.ino          # Arquivo principal do firmware
├── src/
│   ├── sensors/                # Módulos dos sensores (DHT22, MQ-135)
│   ├── actuators/              # Módulos dos atuadores (servo, relé, cooler)
│   ├── tasks/                  # Tasks FreeRTOS
│   ├── web/                    # Interface web embarcada (HTML/CSS/JS)
│   ├── storage/                # Persistência de dados
│   ├── logs/                   # Sistema de logs
│   └── energy/                 # Gerenciamento de energia
├── docs/                       # Documentação acadêmica (ABNT)
├── tests/                      # Testes isolados e de integração
└── README.md
```

---

## 🚀 Como Executar

### Pré-requisitos
- [Arduino IDE](https://www.arduino.cc/en/software) (versão 2.x recomendada) ou PlatformIO
- Suporte ao ESP32 instalado no Arduino IDE
- Bibliotecas necessárias:
  - `DHT sensor library` (Adafruit)
  - `ESP32 Arduino Core`
  - `ArduinoJson`
  - `AsyncWebServer` (ESP Async Web Server)

### Instalação

```bash
# Clone o repositório
git clone https://github.com/seu-usuario/biopreserv.git

# Abra o arquivo principal na Arduino IDE
biopreserv/biopreserv.ino
```

### Configuração Inicial

1. No arquivo de configuração, defina as credenciais iniciais de Wi-Fi
2. Faça o upload do firmware para o ESP32
3. Acesse o IP exibido no monitor serial pelo navegador
4. A partir daí, toda configuração pode ser feita pela interface web — sem regravação!

---

## 📐 Conceitos Aplicados

| Conceito | Aplicação no Projeto |
|---|---|
| **Sistemas Ciberfísicos** | Integração entre sensores físicos, processamento e atuação no ambiente real |
| **Concorrência e Paralelismo** | Tasks FreeRTOS para leitura de sensores, interface web e logs em paralelo |
| **Gerenciamento de Memória** | Monitoramento de heap, stack, flash e controle de alocações |
| **RTOS** | FreeRTOS embarcado no ESP32 para multitarefa sem bloqueios |
| **Gerenciamento de Energia** | Light Sleep e Deep Sleep para otimização de consumo |
| **Observabilidade** | Logs, métricas e séries temporais acessíveis pela interface web |

---

## 👨‍💻 Equipe

| Nome | Contato |
|---|---|
| Gabriela Estefania Uzcategui Perez | gabrielauzcategui81@gmail.com |
| Heitor Roberto Gonçalves | heitorgoncalves142@gmail.com |
| João Pedro Vicentini Couto | joaolivro870@gmail.com |
| Rafaella da Silva Butture | rafaeladasilvabutture@gmail.com |
| Yasmin Victória Farias Leal | minvicflg@gmail.com |

---

## 🏫 Informações Acadêmicas

| Campo | Informação |
|---|---|
| **Universidade** | Pontifícia Universidade Católica do Paraná (PUCPR) |
| **Escola** | Escola Politécnica |
| **Curso** | Engenharia de Software |
| **Disciplina** | Performance em Sistemas Ciberfísicos |
| **Professor** | Fábio Bettio |
| **Período** | 2026/1 |

---

## 📄 Licença

Este projeto foi desenvolvido para fins acadêmicos no âmbito do TDE da PUCPR.

---

<p align="center">
  Feito com 📚 e muita dedicação para proteger o que não tem preço.
</p>
