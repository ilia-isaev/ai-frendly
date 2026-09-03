# ai-frendly
Os projetos desenvolvidos usando IA

## Equipamento usado
- GPU Intel Arc A770 16 GB
- Up 16 GB RAM
- Linux Mint 22.4

## Estrutura dos diretórios
```
ai-frendly
├── bin
├── include
├── lib
├── model
│   ├── Qwen3.8-27B-UD-IQ3_XXS.gguf
│   └── ...
├── scripts
│   └── server_fp16_q3.sh
├── src
│   ├── libusocketsurl
│   └── ...
└── third_party
    ├── tarball
    └── ...
```

## Instalação
- Baixe e instale `opencode`.
- Baixe e instale `Intel OneApi 2026.1`
- Baixe, compile, instale `llama.cpp`.
- Ligue servidor local usando `server_fp16_q3.sh` script.
- Abre `opencode` em projeto a onde quer trabalhar, conecte a llm local.

