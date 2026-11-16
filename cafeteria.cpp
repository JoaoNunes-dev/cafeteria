#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace std;

// ==============================================================
//  Sistema de Cafeteria: produtos, caixa com fila FIFO e estoque
//
//  Arquivos binarios (todos com cabecalho: assinatura + versao):
//    produtos.dat -> registros Produto (cadastro, status A/E)
//    pedidos.dat  -> registros Pedido  (historico, ativo/atendido)
//    estado.dat   -> estoque de graos e cupom do dia
//
//  Registro principal das 5 funcionalidades obrigatorias: Produto.
//  O faturamento nao e gravado: e calculado a partir dos pedidos
//  ativos, entao nunca fica inconsistente com o historico.
// ==============================================================

const int MAX_FILA = 20;
const int GRAMAS_POR_DOSE = 18;
const int ESTOQUE_INICIAL = 5000;
const int ESTOQUE_MAXIMO = 100000000;
const int ESTOQUE_BAIXO = 500;
const int PIN_ADMIN = 1234;
const int TAM_NOME = 40;
const int MAX_QUANTIDADE = 1000;
const int MAX_DOSES = 100;
const float MAX_PRECO = 1000000.0f;
const float MAX_TOTAL = 1000000000.0f;
const unsigned int VERSAO = 1;

const char STATUS_ATIVO = 'A';
const char STATUS_EXCLUIDO = 'E';

const char ARQ_PRODUTOS[] = "produtos.dat";
const char ARQ_PEDIDOS[] = "pedidos.dat";
const char ARQ_ESTADO[] = "estado.dat";
const char ARQ_TEMP[] = "estado.tmp";
const char TXT_PRODUTOS[] = "produtos.txt";
const char TXT_PEDIDOS[] = "pedidos.txt";

// Assinaturas de 8 caracteres (identificam o tipo do arquivo)
const char ASSIN_PRODUTOS[] = "CAFEPROD";
const char ASSIN_PEDIDOS[] = "CAFEPEDI";
const char ASSIN_ESTADO[] = "CAFEESTA";

// Produtos criados na primeira execucao
const int QTD_INICIAIS = 5;
const char *nomesIniciais[QTD_INICIAIS] = {"Espresso", "Espresso duplo", "Cappuccino", "Coado V60", "Pao de queijo"};
const float precosIniciais[QTD_INICIAIS] = {6.00f, 9.00f, 12.00f, 10.00f, 5.50f};
const int dosesIniciais[QTD_INICIAIS] = {1, 2, 1, 1, 0};

struct Cabecalho {
    char assinatura[8];
    unsigned int versao;
};

// Registro principal (cadastro)
struct Produto {
    int codigo;
    char nome[TAM_NOME];
    float preco;
    int doses;      // doses de cafe por unidade (0 = nao usa graos)
    char status;    // 'A' = ativo, 'E' = excluido (exclusao logica)
};

// Historico de vendas (guarda copia do nome para nao depender do cadastro)
struct Pedido {
    int numero;
    int codigo;
    char nome[TAM_NOME];
    int quantidade;
    float total;
    int gramas;     // graos consumidos (usado para devolver ao cancelar)
    int ativo;      // 1 = valido; 0 = cancelado (exclusao logica)
    int atendido;   // 1 = ja saiu da fila
};

struct Estado {
    char assinatura[8];
    unsigned int versao;
    int estoqueGramas;
    int codigoPromocional;
    int dataPromocional;
};

const streamoff TAM_CABECALHO = static_cast<streamoff>(sizeof(Cabecalho));

// Fila circular (FIFO) de pedidos pendentes
Pedido fila[MAX_FILA];
int inicioFila = 0;
int fimFila = 0;
int qtdFila = 0;

int codigoPromocional = 0;
int dataPromocional = 0;

// ---------- Leitura segura ----------

int lerInteiro() {
    string linha;
    int valor;

    while (true) {
        if (!getline(cin, linha)) {
            return 0;
        }
        istringstream leitor(linha);
        if (leitor >> valor) {
            return valor;
        }
        cout << "Entrada invalida. Digite um numero: ";
    }
}

float lerFloat() {
    string linha;
    float valor;

    while (true) {
        if (!getline(cin, linha)) {
            return 0;
        }
        replace(linha.begin(), linha.end(), ',', '.');
        istringstream leitor(linha);
        if (leitor >> valor) {
            return valor;
        }
        cout << "Entrada invalida. Digite um valor: ";
    }
}

string lerTexto() {
    string linha;

    getline(cin, linha);
    return linha;
}

// ---------- Acesso basico aos arquivos binarios ----------

bool arquivoExiste(const char *nomeArquivo) {
    ifstream arq(nomeArquivo, ios::binary);

    if (arq) {
        return true;
    } else {
        return false;
    }
}

bool criarArquivo(const char *nomeArquivo, const char *assinatura) {
    ofstream arq(nomeArquivo, ios::binary | ios::trunc);
    Cabecalho c;

    if (!arq) {
        return false;
    }
    memset(&c, 0, sizeof(Cabecalho));
    memcpy(c.assinatura, assinatura, 8);
    c.versao = VERSAO;
    arq.write(reinterpret_cast<const char *>(&c), sizeof(Cabecalho));
    arq.flush();
    if (arq.good()) {
        return true;
    } else {
        return false;
    }
}

// Le o cabecalho e confere assinatura e versao
bool cabecalhoValido(ifstream &arq, const char *assinatura) {
    Cabecalho c;

    if (!arq.read(reinterpret_cast<char *>(&c), sizeof(Cabecalho))) {
        return false;
    }
    if (memcmp(c.assinatura, assinatura, 8) != 0) {
        return false;
    }
    if (c.versao != VERSAO) {
        return false;
    }
    return true;
}

// Quantidade de registros do arquivo (-1 se o tamanho for invalido)
int contarRegistros(const char *nomeArquivo, size_t tamanho) {
    ifstream arq(nomeArquivo, ios::binary | ios::ate);
    streamoff bytes;
    streamoff corpo;

    if (!arq) {
        return -1;
    }
    bytes = arq.tellg();
    corpo = bytes - TAM_CABECALHO;
    if (corpo < 0 || corpo % static_cast<streamoff>(tamanho) != 0) {
        return -1;
    }
    return static_cast<int>(corpo / static_cast<streamoff>(tamanho));
}

bool lerRegistro(const char *nomeArquivo, int posicao, void *registro, size_t tamanho) {
    ifstream arq(nomeArquivo, ios::binary);

    if (!arq) {
        return false;
    }
    arq.seekg(TAM_CABECALHO + static_cast<streamoff>(posicao) * static_cast<streamoff>(tamanho), ios::beg);
    if (arq.read(static_cast<char *>(registro), tamanho)) {
        return true;
    } else {
        return false;
    }
}

bool gravarRegistro(const char *nomeArquivo, int posicao, const void *registro, size_t tamanho) {
    fstream arq(nomeArquivo, ios::in | ios::out | ios::binary);

    if (!arq) {
        return false;
    }
    arq.seekp(TAM_CABECALHO + static_cast<streamoff>(posicao) * static_cast<streamoff>(tamanho), ios::beg);
    arq.write(static_cast<const char *>(registro), tamanho);
    arq.flush();
    if (arq.good()) {
        return true;
    } else {
        return false;
    }
}

bool anexarRegistro(const char *nomeArquivo, const void *registro, size_t tamanho) {
    ofstream arq(nomeArquivo, ios::binary | ios::app);

    if (!arq) {
        return false;
    }
    arq.write(static_cast<const char *>(registro), tamanho);
    arq.flush();
    if (arq.good()) {
        return true;
    } else {
        return false;
    }
}

// ---------- Produtos ----------

Produto montarProduto(int codigo, const string &nome, float preco, int doses) {
    Produto p;

    memset(&p, 0, sizeof(Produto));
    p.codigo = codigo;
    strncpy(p.nome, nome.c_str(), TAM_NOME - 1);
    p.preco = preco;
    p.doses = doses;
    p.status = STATUS_ATIVO;
    return p;
}

// Posicao (indice) do produto no arquivo, ou -1 se nao existir
int buscarPosicaoProduto(int codigo) {
    ifstream arq(ARQ_PRODUTOS, ios::binary);
    Produto p;
    int posicao = 0;

    arq.seekg(TAM_CABECALHO, ios::beg);
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Produto))) {
        if (p.codigo == codigo) {
            return posicao;
        }
        posicao++;
    }
    return -1;
}

// Busca um produto que exista E esteja ativo
bool buscarProdutoAtivo(int codigo, Produto &p) {
    int posicao = buscarPosicaoProduto(codigo);

    if (posicao == -1) {
        return false;
    }
    if (!lerRegistro(ARQ_PRODUTOS, posicao, &p, sizeof(Produto))) {
        return false;
    }
    if (p.status == STATUS_ATIVO) {
        return true;
    } else {
        return false;
    }
}

int contarProdutos(char status) {
    ifstream arq(ARQ_PRODUTOS, ios::binary);
    Produto p;
    int total = 0;

    arq.seekg(TAM_CABECALHO, ios::beg);
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Produto))) {
        if (p.status == status) {
            total++;
        }
    }
    return total;
}

// Codigo nunca reutilizado: maior codigo ja gravado + 1 (inclui excluidos)
int proximoCodigo() {
    ifstream arq(ARQ_PRODUTOS, ios::binary);
    Produto p;
    int maior = 0;

    arq.seekg(TAM_CABECALHO, ios::beg);
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Produto))) {
        if (p.codigo > maior) {
            maior = p.codigo;
        }
    }
    return maior + 1;
}

bool nomeJaCadastrado(const string &nome) {
    ifstream arq(ARQ_PRODUTOS, ios::binary);
    Produto p;

    arq.seekg(TAM_CABECALHO, ios::beg);
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Produto))) {
        if (p.status == STATUS_ATIVO && strcmp(p.nome, nome.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

bool criarProdutosIniciais() {
    Produto p;

    if (!criarArquivo(ARQ_PRODUTOS, ASSIN_PRODUTOS)) {
        return false;
    }
    for (int i = 0; i < QTD_INICIAIS; i++) {
        p = montarProduto(i + 1, nomesIniciais[i], precosIniciais[i], dosesIniciais[i]);
        if (!anexarRegistro(ARQ_PRODUTOS, &p, sizeof(Produto))) {
            return false;
        }
    }
    return true;
}

// ---------- Pedidos (somente leitura de totais) ----------

void contarPedidos(int &total, int &ativos, int &atendidos, float &faturamento) {
    ifstream arq(ARQ_PEDIDOS, ios::binary);
    Pedido p;

    total = 0;
    ativos = 0;
    atendidos = 0;
    faturamento = 0.0f;
    arq.seekg(TAM_CABECALHO, ios::beg);
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Pedido))) {
        total++;
        if (p.ativo == 1) {
            ativos++;
            faturamento = faturamento + p.total;
            if (p.atendido == 1) {
                atendidos++;
            }
        }
    }
}

// ---------- Estado (estoque e cupom do dia) ----------

// Grava em arquivo temporario e so depois troca pelo definitivo.
// Se o programa parar no meio, o estado anterior (ou o .tmp completo) continua valido.
bool salvarEstado(int estoqueGramas) {
    Estado e;

    memset(&e, 0, sizeof(Estado));
    memcpy(e.assinatura, ASSIN_ESTADO, 8);
    e.versao = VERSAO;
    e.estoqueGramas = estoqueGramas;
    e.codigoPromocional = codigoPromocional;
    e.dataPromocional = dataPromocional;

    ofstream arq(ARQ_TEMP, ios::binary | ios::trunc);
    if (!arq) {
        return false;
    }
    arq.write(reinterpret_cast<const char *>(&e), sizeof(Estado));
    arq.close();
    if (!arq) {
        remove(ARQ_TEMP);
        return false;
    }
    // No Windows o rename nao sobrescreve: remove o antigo antes
    remove(ARQ_ESTADO);
    if (rename(ARQ_TEMP, ARQ_ESTADO) != 0) {
        return false;
    }
    return true;
}

bool carregarEstado(int &estoqueGramas) {
    ifstream arq(ARQ_ESTADO, ios::binary);
    Estado e;

    if (!arq.read(reinterpret_cast<char *>(&e), sizeof(Estado))) {
        return false;
    }
    if (arq.peek() != EOF) {
        return false;
    }
    if (memcmp(e.assinatura, ASSIN_ESTADO, 8) != 0 || e.versao != VERSAO) {
        return false;
    }
    if (e.estoqueGramas < 0 || e.estoqueGramas > ESTOQUE_MAXIMO) {
        return false;
    }
    if (e.codigoPromocional < 1 || e.codigoPromocional > 3) {
        return false;
    }
    estoqueGramas = e.estoqueGramas;
    codigoPromocional = e.codigoPromocional;
    dataPromocional = e.dataPromocional;
    return true;
}

int dataDeHoje() {
    time_t agora = time(NULL);
    tm *data = localtime(&agora);

    return (data->tm_year + 1900) * 10000 + (data->tm_mon + 1) * 100 + data->tm_mday;
}

// ---------- Funcoes do enunciado ----------

// Sem parametro, com retorno: codigo de desconto do dia (sorteado uma vez por dia)
int obterCodigoPromocional() {
    if (codigoPromocional == 0) {
        codigoPromocional = (rand() % 3) + 1;
    }
    return codigoPromocional;
}

int obterDescontoPercentual(int codigo) {
    if (codigo == 1) {
        return 5;
    } else if (codigo == 2) {
        return 10;
    } else if (codigo == 3) {
        return 15;
    } else {
        return 0;
    }
}

// Com parametro, sem retorno
void imprimirComprovantePedido(int idItem, float precoTotal) {
    Produto p;

    cout << "\n====== COMPROVANTE ======\n";
    if (buscarProdutoAtivo(idItem, p)) {
        cout << "Item : " << p.nome << "\n";
    } else {
        cout << "Item : #" << idItem << "\n";
    }
    cout << "Total: R$ " << precoTotal << "\n";
    cout << "=========================\n";
}

// Valor: quantidadeDoses | Referencia: estoqueGramas
void baixarEstoqueGrains(int quantidadeDoses, int &estoqueGramas) {
    int consumo = quantidadeDoses * GRAMAS_POR_DOSE;

    if (consumo <= estoqueGramas) {
        estoqueGramas = estoqueGramas - consumo;
    } else {
        cout << "Estoque insuficiente para baixar.\n";
    }
}

// ---------- Fila FIFO ----------

bool filaCheia() {
    if (qtdFila == MAX_FILA) {
        return true;
    } else {
        return false;
    }
}

bool filaVazia() {
    if (qtdFila == 0) {
        return true;
    } else {
        return false;
    }
}

void enfileirar(Pedido p) {
    fila[fimFila] = p;
    fimFila = (fimFila + 1) % MAX_FILA;
    qtdFila++;
}

Pedido desenfileirar() {
    Pedido p = fila[inicioFila];
    inicioFila = (inicioFila + 1) % MAX_FILA;
    qtdFila--;
    return p;
}

// Refaz a fila a partir do arquivo: pedidos ativos e ainda nao atendidos, na ordem
void reconstruirFila() {
    ifstream arq(ARQ_PEDIDOS, ios::binary);
    Pedido p;

    inicioFila = 0;
    fimFila = 0;
    qtdFila = 0;
    arq.seekg(TAM_CABECALHO, ios::beg);
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Pedido))) {
        if (p.ativo == 1 && p.atendido == 0 && !filaCheia()) {
            enfileirar(p);
        }
    }
}

void exibirFila() {
    int posicao;
    Pedido p;

    if (filaVazia()) {
        cout << "\nNenhum pedido na fila.\n";
        return;
    }

    cout << "\n--- Fila de pedidos (proximo no topo) ---\n";
    for (int i = 0; i < qtdFila; i++) {
        posicao = (inicioFila + i) % MAX_FILA;
        p = fila[posicao];
        cout << i + 1 << ") Pedido #" << p.numero << " - " << p.quantidade << "x "
             << p.nome << " - R$ " << p.total << "\n";
    }
}

// ---------- Validacao dos arquivos na abertura ----------

bool validarProdutos() {
    ifstream arq(ARQ_PRODUTOS, ios::binary);
    Produto p;
    int anterior = 0;

    if (contarRegistros(ARQ_PRODUTOS, sizeof(Produto)) < 0 || !arq) {
        return false;
    }
    if (!cabecalhoValido(arq, ASSIN_PRODUTOS)) {
        return false;
    }
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Produto))) {
        if (p.codigo <= anterior || p.nome[TAM_NOME - 1] != '\0') {
            return false;
        }
        if (!(p.preco > 0 && p.preco <= MAX_PRECO)) {
            return false;
        }
        if (p.doses < 0 || p.doses > MAX_DOSES) {
            return false;
        }
        if (p.status != STATUS_ATIVO && p.status != STATUS_EXCLUIDO) {
            return false;
        }
        anterior = p.codigo;
    }
    return true;
}

bool validarPedidos() {
    ifstream arq(ARQ_PEDIDOS, ios::binary);
    Pedido p;
    int numeroEsperado = 1;
    int pendentes = 0;

    if (contarRegistros(ARQ_PEDIDOS, sizeof(Pedido)) < 0 || !arq) {
        return false;
    }
    if (!cabecalhoValido(arq, ASSIN_PEDIDOS)) {
        return false;
    }
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Pedido))) {
        if (p.numero != numeroEsperado || p.codigo < 1 || p.nome[TAM_NOME - 1] != '\0') {
            return false;
        }
        if (p.quantidade < 1 || p.quantidade > MAX_QUANTIDADE) {
            return false;
        }
        if (!(p.total >= 0 && p.total <= MAX_TOTAL)) {
            return false;
        }
        if (p.gramas < 0 || p.gramas > MAX_QUANTIDADE * MAX_DOSES * GRAMAS_POR_DOSE) {
            return false;
        }
        if ((p.ativo != 0 && p.ativo != 1) || (p.atendido != 0 && p.atendido != 1)) {
            return false;
        }
        if (p.ativo == 1 && p.atendido == 0) {
            pendentes++;
        }
        numeroEsperado++;
    }
    if (pendentes > MAX_FILA) {
        return false;
    }
    return true;
}

// Prepara tudo antes do menu. Se algum arquivo estiver invalido, NAO altera nada.
bool iniciarArquivos(int &estoqueGramas) {
    // Se o programa parou entre "remove" e "rename" do estado, recupera o .tmp
    if (!arquivoExiste(ARQ_ESTADO) && arquivoExiste(ARQ_TEMP)) {
        rename(ARQ_TEMP, ARQ_ESTADO);
    }

    if (!arquivoExiste(ARQ_PRODUTOS)) {
        if (!criarProdutosIniciais()) {
            cerr << "Erro: nao foi possivel criar " << ARQ_PRODUTOS << ".\n";
            return false;
        }
    }
    if (!arquivoExiste(ARQ_PEDIDOS)) {
        if (!criarArquivo(ARQ_PEDIDOS, ASSIN_PEDIDOS)) {
            cerr << "Erro: nao foi possivel criar " << ARQ_PEDIDOS << ".\n";
            return false;
        }
    }

    if (!validarProdutos()) {
        cerr << "Erro: " << ARQ_PRODUTOS << " invalido ou corrompido. Nada foi alterado.\n";
        return false;
    }
    if (!validarPedidos()) {
        cerr << "Erro: " << ARQ_PEDIDOS << " invalido ou corrompido. Nada foi alterado.\n";
        return false;
    }

    if (arquivoExiste(ARQ_ESTADO)) {
        if (!carregarEstado(estoqueGramas)) {
            cerr << "Erro: " << ARQ_ESTADO << " invalido ou corrompido. Nada foi alterado.\n";
            return false;
        }
    } else {
        estoqueGramas = ESTOQUE_INICIAL;
        codigoPromocional = 0;
        dataPromocional = 0;
    }

    reconstruirFila();

    // Cupom do dia: sorteia de novo quando a data muda
    if (dataPromocional != dataDeHoje()) {
        dataPromocional = dataDeHoje();
        codigoPromocional = 0;
        obterCodigoPromocional();
        if (!salvarEstado(estoqueGramas)) {
            cerr << "Erro: nao foi possivel gravar " << ARQ_ESTADO << ".\n";
            return false;
        }
    }
    return true;
}

// ---------- Funcionalidades obrigatorias (registro Produto) ----------

// Lista os produtos ativos e devolve quantos foram exibidos
int exibirCardapio() {
    ifstream arq(ARQ_PRODUTOS, ios::binary);
    Produto p;
    int exibidos = 0;

    cout << "\n--- Cardapio ---\n";
    arq.seekg(TAM_CABECALHO, ios::beg);
    while (arq.read(reinterpret_cast<char *>(&p), sizeof(Produto))) {
        if (p.status == STATUS_ATIVO) {
            cout << left << setw(5) << p.codigo << setw(18) << p.nome << " R$ " << p.preco << "\n";
            exibidos++;
        }
    }
    if (exibidos == 0) {
        cout << "Nenhum produto cadastrado.\n";
    }
    return exibidos;
}

// 1) Cadastrar registros
void cadastrarProduto() {
    string nome;
    float preco;
    int doses;
    Produto p;

    cout << "Nome do produto: ";
    nome = lerTexto();
    if (nome.find_first_not_of(' ') == string::npos) {
        cout << "Nome invalido.\n";
        return;
    }
    if (nome.size() >= static_cast<size_t>(TAM_NOME)) {
        cout << "Nome muito longo (maximo " << TAM_NOME - 1 << " caracteres).\n";
        return;
    }
    if (nomeJaCadastrado(nome)) {
        cout << "Ja existe um produto ativo com esse nome.\n";
        return;
    }

    cout << "Preco: ";
    preco = lerFloat();
    if (!(preco > 0 && preco <= MAX_PRECO)) {
        cout << "Preco invalido.\n";
        return;
    }

    cout << "Doses de cafe por unidade (0 se nao usa graos): ";
    doses = lerInteiro();
    if (doses < 0 || doses > MAX_DOSES) {
        cout << "Valor invalido (0 a " << MAX_DOSES << ").\n";
        return;
    }

    p = montarProduto(proximoCodigo(), nome, preco, doses);
    if (!anexarRegistro(ARQ_PRODUTOS, &p, sizeof(Produto))) {
        cout << "Falha ao gravar o produto.\n";
        return;
    }
    cout << "\nProduto cadastrado com o codigo " << p.codigo << ".\n";
}

// 2) Consultar por codigo
void consultarProduto() {
    int codigo;
    int posicao;
    Produto p;

    cout << "Codigo do produto: ";
    codigo = lerInteiro();

    posicao = buscarPosicaoProduto(codigo);
    if (posicao == -1 || !lerRegistro(ARQ_PRODUTOS, posicao, &p, sizeof(Produto))) {
        cout << "\nProduto nao encontrado.\n";
        return;
    }
    if (p.status == STATUS_EXCLUIDO) {
        cout << "\nProduto excluido (exclusao logica).\n";
        return;
    }

    cout << "\n--- Produto " << p.codigo << " ---\n";
    cout << "Nome  : " << p.nome << "\n";
    cout << "Preco : R$ " << p.preco << "\n";
    cout << "Doses : " << p.doses << " por unidade (" << p.doses * GRAMAS_POR_DOSE << " g de graos)\n";
    cout << "Status: Ativo\n";
}

// 3) Exportar para arquivo texto (produtos ativos e historico de pedidos)
void exportarTXT() {
    ifstream arqProdutos(ARQ_PRODUTOS, ios::binary);
    ifstream arqPedidos(ARQ_PEDIDOS, ios::binary);
    ofstream txtProdutos(TXT_PRODUTOS);
    ofstream txtPedidos(TXT_PEDIDOS);
    Produto produto;
    Pedido pedido;
    int totalProdutos = 0;
    int totalPedidos = 0;

    if (!txtProdutos || !txtPedidos) {
        cout << "Nao foi possivel criar os arquivos TXT.\n";
        return;
    }

    txtProdutos << fixed << setprecision(2) << left;
    txtProdutos << "PRODUTOS CADASTRADOS (ATIVOS)\n";
    txtProdutos << "----------------------------------------------\n";
    txtProdutos << setw(8) << "CODIGO" << setw(22) << "NOME" << setw(12) << "PRECO (R$)" << "DOSES\n";
    txtProdutos << "----------------------------------------------\n";
    arqProdutos.seekg(TAM_CABECALHO, ios::beg);
    while (arqProdutos.read(reinterpret_cast<char *>(&produto), sizeof(Produto))) {
        if (produto.status == STATUS_ATIVO) {
            txtProdutos << setw(8) << produto.codigo << setw(22) << produto.nome
                        << setw(12) << produto.preco << produto.doses << "\n";
            totalProdutos++;
        }
    }
    txtProdutos << "----------------------------------------------\n";
    txtProdutos << "Total de produtos ativos: " << totalProdutos << "\n";

    txtPedidos << fixed << setprecision(2) << left;
    txtPedidos << "HISTORICO DE PEDIDOS\n";
    txtPedidos << "------------------------------------------------------------------\n";
    txtPedidos << setw(6) << "ID" << setw(20) << "PRODUTO" << setw(6) << "QTD"
               << setw(12) << "TOTAL (R$)" << setw(12) << "STATUS" << "ATENDIMENTO\n";
    txtPedidos << "------------------------------------------------------------------\n";
    arqPedidos.seekg(TAM_CABECALHO, ios::beg);
    while (arqPedidos.read(reinterpret_cast<char *>(&pedido), sizeof(Pedido))) {
        txtPedidos << setw(6) << pedido.numero << setw(20) << pedido.nome << setw(6) << pedido.quantidade
                   << setw(12) << pedido.total;
        if (pedido.ativo == 1) {
            txtPedidos << setw(12) << "ativo";
        } else {
            txtPedidos << setw(12) << "cancelado";
        }
        if (pedido.atendido == 1) {
            txtPedidos << "atendido\n";
        } else {
            txtPedidos << "pendente\n";
        }
        totalPedidos++;
    }
    txtPedidos << "------------------------------------------------------------------\n";
    txtPedidos << "Total de pedidos registrados: " << totalPedidos << "\n";

    txtProdutos.close();
    txtPedidos.close();
    if (!txtProdutos || !txtPedidos) {
        cout << "Falha ao gravar os arquivos TXT.\n";
        return;
    }
    cout << "\n" << totalProdutos << " produto(s) exportado(s) para " << TXT_PRODUTOS << ".\n";
    cout << totalPedidos << " pedido(s) exportado(s) para " << TXT_PEDIDOS << ".\n";
}

// 4) Excluir registros (exclusao logica: so muda o campo status)
void excluirProduto() {
    int codigo;
    int posicao;
    int confirmacao;
    Produto p;

    if (exibirCardapio() == 0) {
        return;
    }
    cout << "Codigo do produto a excluir: ";
    codigo = lerInteiro();

    posicao = buscarPosicaoProduto(codigo);
    if (posicao == -1 || !lerRegistro(ARQ_PRODUTOS, posicao, &p, sizeof(Produto))) {
        cout << "\nProduto nao encontrado.\n";
        return;
    }
    if (p.status == STATUS_EXCLUIDO) {
        cout << "\nProduto ja esta excluido.\n";
        return;
    }

    cout << "Excluir \"" << p.nome << "\"? (1 - Sim / 0 - Nao): ";
    confirmacao = lerInteiro();
    if (confirmacao != 1) {
        cout << "Exclusao cancelada.\n";
        return;
    }

    p.status = STATUS_EXCLUIDO;
    if (!gravarRegistro(ARQ_PRODUTOS, posicao, &p, sizeof(Produto))) {
        cout << "Falha ao gravar. Produto nao excluido.\n";
        return;
    }
    cout << "\nProduto excluido (o registro permanece no arquivo com status 'E').\n";
}

// 5) Contabilizar registros: aparece no cabecalho do menu e no relatorio

// ---------- Operacoes do caixa ----------

bool estoqueSuficiente(int quantidadeDoses, int estoqueGramas) {
    if (quantidadeDoses * GRAMAS_POR_DOSE <= estoqueGramas) {
        return true;
    } else {
        return false;
    }
}

void novoPedido(int &estoqueGramas) {
    int codigo;
    int quantidade;
    int doses;
    int codigoPromo;
    int desconto;
    int totalRegistros;
    int estoqueAnterior;
    float total;
    Produto produto;
    Pedido p;

    if (filaCheia()) {
        cout << "\nFila cheia! Atenda algum pedido antes de registrar outro.\n";
        return;
    }
    if (exibirCardapio() == 0) {
        return;
    }

    cout << "Codigo do produto: ";
    codigo = lerInteiro();
    if (!buscarProdutoAtivo(codigo, produto)) {
        cout << "Produto invalido ou excluido.\n";
        return;
    }

    cout << "Quantidade: ";
    quantidade = lerInteiro();
    if (quantidade < 1 || quantidade > MAX_QUANTIDADE) {
        cout << "Quantidade invalida (1 a " << MAX_QUANTIDADE << ").\n";
        return;
    }

    doses = produto.doses * quantidade;
    if (!estoqueSuficiente(doses, estoqueGramas)) {
        cout << "Estoque de graos insuficiente para este pedido.\n";
        return;
    }

    codigoPromo = obterCodigoPromocional();
    desconto = obterDescontoPercentual(codigoPromo);
    total = produto.preco * static_cast<float>(quantidade);
    total = total - (total * static_cast<float>(desconto) / 100.0f);
    if (!(total >= 0 && total <= MAX_TOTAL)) {
        cout << "Pedido excede os limites do sistema.\n";
        return;
    }

    totalRegistros = contarRegistros(ARQ_PEDIDOS, sizeof(Pedido));
    if (totalRegistros < 0) {
        cout << "Erro ao ler o arquivo de pedidos.\n";
        return;
    }

    memset(&p, 0, sizeof(Pedido));
    p.numero = totalRegistros + 1;
    p.codigo = produto.codigo;
    strncpy(p.nome, produto.nome, TAM_NOME - 1);
    p.quantidade = quantidade;
    p.total = total;
    p.gramas = doses * GRAMAS_POR_DOSE;
    p.ativo = 1;
    p.atendido = 0;

    // 1o grava o pedido; 2o grava o estoque. Se o 2o falhar, cancela o pedido.
    if (!anexarRegistro(ARQ_PEDIDOS, &p, sizeof(Pedido))) {
        cout << "Falha ao gravar o pedido. Nada foi registrado.\n";
        return;
    }
    estoqueAnterior = estoqueGramas;
    baixarEstoqueGrains(doses, estoqueGramas);
    if (!salvarEstado(estoqueGramas)) {
        estoqueGramas = estoqueAnterior;
        p.ativo = 0;
        gravarRegistro(ARQ_PEDIDOS, p.numero - 1, &p, sizeof(Pedido));
        cout << "Falha ao salvar o estoque. Pedido cancelado.\n";
        return;
    }
    enfileirar(p);

    cout << "\nPedido #" << p.numero << " registrado. Cupom do dia: codigo " << codigoPromo
         << " (" << desconto << "% de desconto)\n";
    imprimirComprovantePedido(produto.codigo, total);

    if (estoqueGramas < ESTOQUE_BAIXO) {
        cout << "Atencao: estoque de graos baixo (" << estoqueGramas << " g).\n";
    }
}

void atenderPedido() {
    Pedido p;

    if (filaVazia()) {
        cout << "\nNao ha pedidos na fila.\n";
        return;
    }

    p = fila[inicioFila];
    p.atendido = 1;
    if (!gravarRegistro(ARQ_PEDIDOS, p.numero - 1, &p, sizeof(Pedido))) {
        cout << "\nFalha ao gravar. O pedido permanece na fila.\n";
        return;
    }
    desenfileirar();
    cout << "\nAtendendo pedido #" << p.numero << ": " << p.quantidade << "x " << p.nome << "\n";
}

// ---------- Administracao ----------

bool alternarModoAdmin(bool modoAtual) {
    int pin;

    if (modoAtual) {
        cout << "\nSaindo do modo administracao.\n";
        return false;
    }

    cout << "PIN de administrador: ";
    pin = lerInteiro();
    if (pin == PIN_ADMIN) {
        cout << "\nModo administracao ativado.\n";
        return true;
    } else {
        cout << "\nPIN incorreto.\n";
        return false;
    }
}

bool exigirAdmin(bool modoAdmin) {
    if (modoAdmin) {
        return true;
    } else {
        cout << "\nAcesso restrito ao modo administracao.\n";
        return false;
    }
}

void reporEstoque(int &estoqueGramas) {
    int gramas;
    int estoqueAnterior;

    cout << "Estoque atual: " << estoqueGramas << " g\n";
    cout << "Gramas a repor: ";
    gramas = lerInteiro();
    if (gramas < 1 || gramas > ESTOQUE_MAXIMO - estoqueGramas) {
        cout << "Valor invalido.\n";
        return;
    }

    estoqueAnterior = estoqueGramas;
    estoqueGramas = estoqueGramas + gramas;
    if (!salvarEstado(estoqueGramas)) {
        estoqueGramas = estoqueAnterior;
        cout << "Falha ao salvar. Estoque nao alterado.\n";
        return;
    }
    cout << "Novo estoque: " << estoqueGramas << " g\n";
}

void alterarPreco() {
    int codigo;
    int posicao;
    float novoPreco;
    Produto p;

    if (exibirCardapio() == 0) {
        return;
    }
    cout << "Codigo do produto: ";
    codigo = lerInteiro();

    posicao = buscarPosicaoProduto(codigo);
    if (posicao == -1 || !lerRegistro(ARQ_PRODUTOS, posicao, &p, sizeof(Produto)) || p.status != STATUS_ATIVO) {
        cout << "Produto invalido ou excluido.\n";
        return;
    }

    cout << "Novo preco: ";
    novoPreco = lerFloat();
    if (!(novoPreco > 0 && novoPreco <= MAX_PRECO)) {
        cout << "Preco invalido.\n";
        return;
    }

    p.preco = novoPreco;
    if (!gravarRegistro(ARQ_PRODUTOS, posicao, &p, sizeof(Produto))) {
        cout << "Falha ao gravar. Preco nao alterado.\n";
        return;
    }
    cout << p.nome << " agora custa R$ " << novoPreco << " (pedidos anteriores nao mudam)\n";
}

void consultarPedido() {
    int id;
    int total;
    Pedido p;

    cout << "ID do pedido: ";
    id = lerInteiro();

    total = contarRegistros(ARQ_PEDIDOS, sizeof(Pedido));
    if (id < 1 || id > total || !lerRegistro(ARQ_PEDIDOS, id - 1, &p, sizeof(Pedido))) {
        cout << "\nPedido nao encontrado.\n";
        return;
    }

    cout << "\n--- Pedido #" << p.numero << " ---\n";
    cout << "Produto    : " << p.nome << " (codigo " << p.codigo << ")\n";
    cout << "Quantidade : " << p.quantidade << "\n";
    cout << "Total      : R$ " << p.total << "\n";
    if (p.ativo == 1) {
        cout << "Status     : ativo\n";
    } else {
        cout << "Status     : cancelado\n";
    }
    if (p.atendido == 1) {
        cout << "Atendimento: atendido\n";
    } else {
        cout << "Atendimento: pendente\n";
    }
}

// Cancelamento = exclusao logica do pedido (ativo = 0); devolve os graos se ainda nao atendido
void cancelarPedido(int &estoqueGramas) {
    int id;
    int total;
    int devolucao;
    int confirmacao;
    int estoqueAnterior;
    Pedido p;

    cout << "ID do pedido a cancelar: ";
    id = lerInteiro();

    total = contarRegistros(ARQ_PEDIDOS, sizeof(Pedido));
    if (id < 1 || id > total || !lerRegistro(ARQ_PEDIDOS, id - 1, &p, sizeof(Pedido))) {
        cout << "\nPedido nao encontrado.\n";
        return;
    }
    if (p.ativo == 0) {
        cout << "\nPedido ja esta cancelado.\n";
        return;
    }

    if (p.atendido == 1) {
        devolucao = 0;
    } else {
        devolucao = p.gramas;
    }
    if (devolucao > ESTOQUE_MAXIMO - estoqueGramas) {
        cout << "Nao foi possivel devolver o estoque.\n";
        return;
    }

    cout << "Cancelar o pedido #" << p.numero << " (" << p.quantidade << "x " << p.nome << ")? (1 - Sim / 0 - Nao): ";
    confirmacao = lerInteiro();
    if (confirmacao != 1) {
        cout << "Cancelamento abortado.\n";
        return;
    }

    estoqueAnterior = estoqueGramas;
    estoqueGramas = estoqueGramas + devolucao;
    if (devolucao > 0) {
        if (!salvarEstado(estoqueGramas)) {
            estoqueGramas = estoqueAnterior;
            cout << "Falha ao salvar. Pedido nao cancelado.\n";
            return;
        }
    }

    p.ativo = 0;
    if (!gravarRegistro(ARQ_PEDIDOS, id - 1, &p, sizeof(Pedido))) {
        estoqueGramas = estoqueAnterior;
        salvarEstado(estoqueGramas);
        cout << "Falha ao gravar. Pedido nao cancelado.\n";
        return;
    }

    reconstruirFila();
    cout << "Pedido #" << id << " cancelado.";
    if (devolucao > 0) {
        cout << " " << devolucao << " g de graos devolvidos ao estoque.";
    }
    cout << "\n";
}

void exibirRelatorio(int estoqueGramas) {
    int total;
    int ativos;
    int atendidos;
    float faturamento;

    contarPedidos(total, ativos, atendidos, faturamento);

    cout << "\n====== RELATORIO ======\n";
    cout << "Produtos ativos     : " << contarProdutos(STATUS_ATIVO) << "\n";
    cout << "Produtos excluidos  : " << contarProdutos(STATUS_EXCLUIDO) << "\n";
    cout << "Pedidos registrados : " << total << "\n";
    cout << "Pedidos ativos      : " << ativos << "\n";
    cout << "Pedidos cancelados  : " << total - ativos << "\n";
    cout << "Pedidos atendidos   : " << atendidos << "\n";
    cout << "Pedidos na fila     : " << qtdFila << "\n";
    cout << "Faturamento         : R$ " << faturamento << "\n";
    cout << "Estoque de graos    : " << estoqueGramas << " g (~" << estoqueGramas / GRAMAS_POR_DOSE << " doses)\n";
    cout << "Codigo promo do dia : " << obterCodigoPromocional() << "\n";
    cout << "=======================\n";
}

// ---------- Menu ----------

void exibirMenu(bool modoAdmin, int estoqueGramas) {
    if (modoAdmin) {
        cout << "\n===== CAFETERIA [ADMIN] =====\n";
    } else {
        cout << "\n===== CAFETERIA [CAIXA] =====\n";
    }
    cout << "Produtos ativos: " << contarProdutos(STATUS_ATIVO) << " | Na fila: " << qtdFila
         << " | Graos: " << estoqueGramas << " g\n";
    cout << "-----------------------------\n";
    cout << "1  - Novo pedido\n";
    cout << "2  - Atender proximo da fila\n";
    cout << "3  - Ver fila\n";
    cout << "4  - Ver cardapio\n";
    cout << "5  - Consultar produto por codigo\n";
    if (modoAdmin) {
        cout << "6  - Sair do modo administracao\n";
        cout << "7  - Cadastrar produto\n";
        cout << "8  - Excluir produto\n";
        cout << "9  - Alterar preco\n";
        cout << "10 - Repor estoque de graos\n";
        cout << "11 - Consultar pedido por ID\n";
        cout << "12 - Cancelar pedido\n";
        cout << "13 - Exportar para TXT\n";
        cout << "14 - Relatorio\n";
    } else {
        cout << "6  - Entrar no modo administracao\n";
    }
    cout << "0  - Encerrar\n";
    cout << "Opcao: ";
}

int main() {
    int opcao;
    bool modoAdmin = false;
    int estoqueGramas = ESTOQUE_INICIAL;

    srand(static_cast<unsigned>(time(NULL)));
    cout << fixed << setprecision(2);

    if (!iniciarArquivos(estoqueGramas)) {
        return 1;
    }

    do {
        exibirMenu(modoAdmin, estoqueGramas);
        opcao = lerInteiro();

        switch (opcao) {
            case 1:
                novoPedido(estoqueGramas);
                break;
            case 2:
                atenderPedido();
                break;
            case 3:
                exibirFila();
                break;
            case 4:
                exibirCardapio();
                break;
            case 5:
                consultarProduto();
                break;
            case 6:
                modoAdmin = alternarModoAdmin(modoAdmin);
                break;
            case 7:
                if (exigirAdmin(modoAdmin)) {
                    cadastrarProduto();
                }
                break;
            case 8:
                if (exigirAdmin(modoAdmin)) {
                    excluirProduto();
                }
                break;
            case 9:
                if (exigirAdmin(modoAdmin)) {
                    alterarPreco();
                }
                break;
            case 10:
                if (exigirAdmin(modoAdmin)) {
                    reporEstoque(estoqueGramas);
                }
                break;
            case 11:
                if (exigirAdmin(modoAdmin)) {
                    consultarPedido();
                }
                break;
            case 12:
                if (exigirAdmin(modoAdmin)) {
                    cancelarPedido(estoqueGramas);
                }
                break;
            case 13:
                if (exigirAdmin(modoAdmin)) {
                    exportarTXT();
                }
                break;
            case 14:
                if (exigirAdmin(modoAdmin)) {
                    exibirRelatorio(estoqueGramas);
                }
                break;
            case 0:
                cout << "\nEncerrando. Ate logo!\n";
                break;
            default:
                cout << "\nOpcao invalida.\n";
        }
    } while (opcao != 0);

    return 0;
}
