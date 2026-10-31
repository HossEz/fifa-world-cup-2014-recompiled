#include "i18n.h"

#include <atomic>
#include <unordered_map>

namespace launcher {

namespace {

// English, Spanish (Latin America), Portuguese (Brazil).
struct Entry {
  const char* en;
  const char* es;
  const char* pt;
};

const Entry kEntries[] = {
    // Home and the button hints.
    {"Play", "Jugar", "Jogar"},
    {"Settings", "Ajustes", "Configurações"},
    {"Controls", "Controles", "Controles"},
    {"About", "Acerca de", "Sobre"},
    {"Next song", "Siguiente canción", "Próxima música"},
    {"Select", "Seleccionar", "Selecionar"},
    {"Quit", "Salir", "Sair"},
    {"Back", "Volver", "Voltar"},
    {"Change", "Cambiar", "Mudar"},
    {"Attacking", "Ataque", "Ataque"},
    {"Defending", "Defensa", "Defesa"},
    {"Continue", "Continuar", "Continuar"},
    {"Later", "Más tarde", "Mais tarde"},
    {"Cancel", "Cancelar", "Cancelar"},
    {"Sure? Press again", "¿Seguro? Presiona otra vez", "Tem certeza? Aperte de novo"},
    {"Quit the launcher?", "¿Salir del launcher?", "Sair do launcher?"},
    {"Yes", "Sí", "Sim"},
    {"No", "No", "Não"},

    // Settings.
    {"Display", "Pantalla", "Tela"},
    {"Graphics", "Gráficos", "Gráficos"},
    {"Menu sound", "Sonido", "Som do menu"},
    {"Data", "Datos", "Dados"},
    {"Off", "Desactivado", "Desligado"},
    {"On", "Activado", "Ligado"},
    {"Language", "Idioma", "Idioma"},
    {"The language of this menu and of the game, which asks for it at every start with this "
     "one selected.",
     "El idioma de este menú y del juego, que lo pregunta en cada inicio con este ya "
     "seleccionado.",
     "O idioma deste menu e do jogo, que pergunta por ele a cada início com este já "
     "selecionado."},
    {"Window", "Ventana", "Janela"},
    {"Fullscreen fills your screen and lets you switch apps quickly. Exclusive gives the game "
     "the screen to itself. Windowed uses Window size.",
     "Pantalla completa llena tu pantalla y te deja cambiar de app rápido. Exclusiva le da la "
     "pantalla solo al juego. En ventana usa el Tamaño de ventana.",
     "Tela cheia ocupa a tela toda e deixa trocar de app rápido. Exclusiva dá a tela só para o "
     "jogo. Em janela usa o Tamanho da janela."},
    {"Fullscreen", "Pantalla completa", "Tela cheia"},
    {"Exclusive", "Exclusiva", "Exclusiva"},
    {"Windowed", "En ventana", "Em janela"},
    {"Window size", "Tamaño de ventana", "Tamanho da janela"},
    {"The size of the game's window in Windowed mode. Automatic makes it as large as fits your "
     "screen. A size larger than your screen is shrunk to fit.",
     "El tamaño de la ventana del juego en modo En ventana. Automático la hace tan grande como "
     "quepa en tu pantalla. Un tamaño mayor que tu pantalla se reduce para que quepa.",
     "O tamanho da janela do jogo no modo Em janela. Automático deixa a janela no maior tamanho "
     "que couber na tela. Um tamanho maior que a tela é reduzido para caber."},
    {"Automatic", "Automático", "Automático"},
    {"Monitor", "Monitor", "Monitor"},
    {"The screen the game opens on.", "La pantalla en la que se abre el juego.",
     "A tela em que o jogo abre."},
    {"Main", "Principal", "Principal"},
    {"Monitor {0}", "Monitor {0}", "Monitor {0}"},
    {"On stops tearing and keeps the game at your screen's refresh rate or below. Off lets Frame "
     "rate go higher, but can tear.",
     "Activado evita el tearing y mantiene el juego a la frecuencia de tu pantalla o menos. "
     "Desactivado permite una tasa más alta, pero puede haber tearing.",
     "Ligado evita o tearing e mantém o jogo na taxa da sua tela ou abaixo. Desligado permite uma "
     "taxa maior, mas pode ter tearing."},
    {"Picture", "Imagen", "Imagem"},
    {"Fit keeps the right shape, with black bars on screens wider than 16:9. Stretch fills the "
     "screen, but players and the HUD look wider.",
     "Ajustar mantiene la forma correcta, con barras negras en pantallas más anchas que 16:9. "
     "Estirar llena la pantalla, pero los jugadores y el HUD se ven más anchos.",
     "Ajustar mantém a forma correta, com barras pretas em telas mais largas que 16:9. Esticar "
     "preenche a tela, mas os jogadores e o HUD ficam mais largos."},
    {"Fit", "Ajustar", "Ajustar"},
    {"Stretch", "Estirar", "Esticar"},
    {"Frame rate", "Tasa de fotogramas", "Taxa de quadros"},
    {"The game holds cutscenes at 40 FPS and many menus at 30, as on the console. These rates "
     "lift both limits. Above 60 needs a faster PC, and heavy scenes may run below the rate you "
     "pick.",
     "El juego limita las escenas a 40 FPS y muchos menús a 30, como en la consola. Estas tasas "
     "quitan ambos límites. Más de 60 requiere un PC más rápido, y las escenas exigentes pueden "
     "ir más lentas.",
     "O jogo limita as cenas a 40 FPS e muitos menus a 30, como no console. Estas taxas tiram os "
     "dois limites. Acima de 60 exige um PC mais rápido, e cenas pesadas podem ficar abaixo."},
    {"As on console", "Como en consola", "Como no console"},
    {"Show FPS", "Mostrar FPS", "Mostrar FPS"},
    {"The frame rate in the top left corner of the game.",
     "Los FPS en la esquina superior izquierda del juego.",
     "Os FPS no canto superior esquerdo do jogo."},
    {"Resolution", "Resolución", "Resolução"},
    {"What the game draws at. 720p is the console's own. 1440p is sharp on a 1080p screen. 4K "
     "needs a fast graphics card.",
     "La resolución a la que dibuja el juego. 720p es la de la consola. 1440p se ve nítido en "
     "una pantalla 1080p. 4K necesita una tarjeta gráfica potente.",
     "A resolução em que o jogo desenha. 720p é a do console. 1440p fica nítido numa tela "
     "1080p. 4K precisa de uma placa de vídeo potente."},
    {"Upscaling", "Escalado", "Escala"},
    {"FSR sharpens the picture as it's fitted to your screen, most of all at 720p on a 1080p "
     "screen. It can look a bit artificial.",
     "FSR hace más nítida la imagen al ajustarla a tu pantalla, sobre todo a 720p en una "
     "pantalla 1080p. Puede verse algo artificial.",
     "O FSR deixa a imagem mais nítida ao ajustá-la à tela, principalmente em 720p numa tela "
     "1080p. Pode parecer um pouco artificial."},
    {"Texture filtering", "Filtrado de texturas", "Filtragem de texturas"},
    {"Keeps the pitch markings and the stands sharp at shallow angles. Costs next to nothing on "
     "today's graphics cards.",
     "Mantiene nítidas las líneas de la cancha y las tribunas en ángulos bajos. Casi no cuesta "
     "rendimiento en las tarjetas gráficas actuales.",
     "Mantém nítidas as linhas do campo e as arquibancadas em ângulos baixos. Quase não pesa nas "
     "placas de vídeo atuais."},
    {"Game's own", "La del juego", "A do jogo"},
    {"Only in this menu. The game's own sound is set in the game.",
     "Solo en este menú. El sonido del juego se ajusta en el juego.",
     "Só neste menu. O som do jogo é ajustado no jogo."},
    {"Music", "Música", "Música"},
    {"The game's EA Trax soundtrack, shuffled, while you're in this menu.",
     "La banda sonora EA Trax del juego, en orden aleatorio, mientras estás en este menú.",
     "A trilha sonora EA Trax do jogo, em ordem aleatória, enquanto você está neste menu."},
    {"Music volume", "Volumen de la música", "Volume da música"},
    {"How loud this menu's music is.", "Qué tan fuerte suena la música de este menú.",
     "O volume da música deste menu."},
    {"Sounds", "Sonidos", "Sons"},
    {"The game's menu sounds as you move, select and go back.",
     "Los sonidos del menú del juego al moverte, seleccionar y volver.",
     "Os sons de menu do jogo ao navegar, selecionar e voltar."},
    {"Sounds volume", "Volumen de los sonidos", "Volume dos sons"},
    {"How loud this menu's sounds are.", "Qué tan fuertes suenan los sonidos de este menú.",
     "O volume dos sons deste menu."},
    {"Game", "Juego", "Jogo"},
    {"Your copy of the game, read from {0}. Change it if you've moved it, or to use its .iso "
     "instead.",
     "Tu copia del juego, ubicada en {0}. Cámbiala si la moviste, o para usar su .iso.",
     "Sua cópia do jogo, lida de {0}. Altere se você a moveu, ou para usar o .iso dela."},
    {"Saves", "Partidas guardadas", "Jogos salvos"},
    {"Open folder", "Abrir carpeta", "Abrir pasta"},
    {"Your profile, settings and tournament saves.",
     "Tu perfil, tus ajustes y tus torneos guardados.",
     "Seu perfil, suas configurações e seus torneios salvos."},
    {"Shader cache", "Caché de shaders", "Cache de shaders"},
    {"Clear", "Borrar", "Limpar"},
    {"Shaders the game has built, kept so later matches don't stutter. Clearing only helps if "
     "graphics look wrong after an update.",
     "Shaders que el juego ya creó, guardados para que los siguientes partidos no tengan "
     "tirones. Borrarlos solo sirve si los gráficos se ven mal tras una actualización.",
     "Shaders que o jogo já criou, guardados para que as próximas partidas não travem. Limpar só "
     "ajuda se os gráficos ficarem errados depois de uma atualização."},
    {"Logs", "Registros", "Registros"},
    {"The game's log (fifawc14.log). Attach it when you report a problem.",
     "El registro del juego (fifawc14.log). Adjúntalo cuando reportes un problema.",
     "O registro do jogo (fifawc14.log). Anexe-o quando relatar um problema."},
    {"Check for updates", "Buscar actualizaciones", "Procurar atualizações"},
    {"At start, asks this project's GitHub page whether a newer version is out, and offers it. "
     "Nothing is sent about you or your game.",
     "Al iniciar, consulta la página de GitHub del proyecto por si hay una versión nueva y te la "
     "ofrece. No se envía nada sobre ti ni sobre tu juego.",
     "Ao iniciar, consulta a página do projeto no GitHub para ver se há uma versão nova e a "
     "oferece. Nada sobre você ou seu jogo é enviado."},
    {"Desktop shortcut", "Acceso directo", "Atalho"},
    {"Create", "Crear", "Criar"},
    // The game's quit shortcut: "Hold [Back] + [Start] to quit", then "Quitting".
    {"Hold", "Mantén", "Segure"},
    {"to quit", "para salir", "para sair"},
    {"Quitting", "Saliendo", "Saindo"},
    {"On desktop", "En el escritorio", "Na área de trabalho"},
    {"The shortcut is on your desktop.", "El acceso directo está en tu escritorio.",
     "O atalho está na sua área de trabalho."},
    {"The shortcut couldn't be created.", "No se pudo crear el acceso directo.",
     "Não foi possível criar o atalho."},
    {"A shortcut to this launcher on your desktop, with the game's icon. Useful for the portable "
     "version, or if you deleted the one the installer made.",
     "Un acceso directo a este launcher en tu escritorio, con el ícono del juego. Útil para la "
     "versión portable o si borraste el que creó el instalador.",
     "Um atalho para este launcher na área de trabalho, com o ícone do jogo. Útil na versão "
     "portátil ou se você apagou o que o instalador criou."},

    // Controls.
    {"A controller works as it does on the console. On a keyboard each button has its own keys. "
     "Both work at any time.",
     "El control funciona igual que en la consola. En el teclado, cada botón tiene sus propias "
     "teclas. Ambos funcionan en cualquier momento.",
     "O controle funciona como no console. No teclado, cada botão tem suas próprias teclas. Os "
     "dois funcionam a qualquer momento."},
    {"CLASSIC PRESET", "CONFIGURACIÓN CLÁSICA", "CONFIGURAÇÃO CLÁSSICA"},
    {"ATTACKING", "ATAQUE", "ATAQUE"},
    {"DEFENDING", "DEFENSA", "DEFESA"},
    {"Short Pass/Header", "Pase corto/Cabezazo", "Passe curto/Cabeceio"},
    {"Contain", "Contener", "Conter"},
    {"Shoot/Volley/Header", "Disparo/Volea/Cabezazo", "Chute/Voleio/Cabeceio"},
    {"Tackle/Push or Pull (when chasing)", "Entrada/Empujar o sujetar (al perseguir)",
     "Desarme/Empurrar ou puxar (ao perseguir)"},
    {"Lob Pass/Cross/Header", "Pase bombeado/Centro/Cabezazo",
     "Passe por cima/Cruzamento/Cabeceio"},
    {"Through Ball", "Pase en profundidad", "Passe em profundidade"},
    {"Player Run/Modifier", "Desmarque/Modificador", "Corrida/Modificador"},
    {"Change Player", "Cambiar jugador", "Trocar jogador"},
    {"Finesse Shot Modifier", "Modificador de disparo colocado", "Modificador de chute colocado"},
    {"Teammate Contain", "Contención de compañero", "Contenção do companheiro"},
    {"Pace Control", "Control de ritmo", "Controle de ritmo"},
    {"Jockey", "Jockey", "Jockey"},
    {"Sprint", "Sprint", "Correr"},
    {"Move Player", "Mover jugador", "Mover jogador"},
    {"Skill moves", "Filigranas", "Dribles"},
    {"Tactics & Mentality", "Tácticas y mentalidad", "Táticas e mentalidade"},
    {"Pause", "Pausa", "Pausar"},
    {"Arrow keys", "Flechas", "Setas"},

    // About.
    {"This project", "Este proyecto", "Este projeto"},
    {"2014 FIFA World Cup Brazil, the Xbox 360 game, running natively on Windows. An unofficial "
     "fan project, ported by hosse.",
     "2014 FIFA World Cup Brazil, el juego de Xbox 360, funcionando de forma nativa en Windows. "
     "Un proyecto no oficial hecho por fans, portado por hosse.",
     "2014 FIFA World Cup Brazil, o jogo de Xbox 360, rodando nativamente no Windows. Um projeto "
     "não oficial feito por fãs, portado por hosse."},
    {"The original, with its source and downloads, is on GitHub:",
     "El original, con su código fuente y descargas, está en GitHub:",
     "O original, com o código-fonte e os downloads, está no GitHub:"},
    {"It is a static recompilation, not an emulator. The game's PowerPC code was translated into "
     "C++ and compiled for your PC, so it runs the game's own logic directly instead of "
     "interpreting Xbox instructions.",
     "Es una recompilación estática, no un emulador. El código PowerPC del juego se tradujo a "
     "C++ y se compiló para tu PC, así que ejecuta la lógica del propio juego directamente en "
     "lugar de interpretar instrucciones de Xbox.",
     "É uma recompilação estática, não um emulador. O código PowerPC do jogo foi traduzido para "
     "C++ e compilado para o seu PC, então ele roda a lógica do próprio jogo diretamente, em vez "
     "de interpretar instruções do Xbox."},
    {"How it works", "Cómo funciona", "Como funciona"},
    {"The recompiled code runs on a runtime that stands in for the console, with the Xbox "
     "kernel's functions, its file system and its threads.",
     "El código recompilado corre sobre un runtime que hace las veces de la consola, con las "
     "funciones del kernel de Xbox, su sistema de archivos y sus hilos.",
     "O código recompilado roda sobre um runtime que faz o papel do console, com as funções do "
     "kernel do Xbox, seu sistema de arquivos e suas threads."},
    {"Every frame, the Xenos GPU's commands are translated to Direct3D 12, its EDRAM render "
     "targets included. The game's XMA audio is decoded with FFmpeg. A few fixes on top let "
     "menus and cutscenes run at 60 FPS and keep the stadium lighting right without slowing the "
     "GPU down.",
     "En cada fotograma, los comandos de la GPU Xenos se traducen a Direct3D 12, incluidos sus "
     "render targets de EDRAM. El audio XMA del juego se decodifica con FFmpeg. Algunas "
     "correcciones adicionales permiten que los menús y las escenas corran a 60 FPS y mantienen "
     "bien la iluminación del estadio sin hacer más lenta la GPU.",
     "A cada quadro, os comandos da GPU Xenos são traduzidos para Direct3D 12, incluindo os "
     "render targets da EDRAM. O áudio XMA do jogo é decodificado com o FFmpeg. Algumas "
     "correções extras fazem os menus e as cenas rodarem a 60 FPS e mantêm a iluminação do "
     "estádio correta sem deixar a GPU mais lenta."},
    {"What it needs", "Requisitos", "Requisitos"},
    {"Windows 10 or 11, 64-bit, a graphics card with Direct3D 12 and up-to-date drivers, and a "
     "processor with SSE4.2.",
     "Windows 10 u 11 de 64 bits, una tarjeta gráfica con Direct3D 12 y drivers actualizados, y "
     "un procesador con SSE4.2.",
     "Windows 10 ou 11 de 64 bits, uma placa de vídeo com Direct3D 12 e drivers atualizados, e "
     "um processador com SSE4.2."},
    {"A controller is recommended; the keyboard works too (its keys are on the Controls page).",
     "Se recomienda un control; el teclado también funciona (sus teclas están en la página "
     "Controles).",
     "É recomendado usar um controle; o teclado também funciona (as teclas estão na página "
     "Controles)."},
    {"Your own copy of the game. Everything of the game's, including the art and music in this "
     "menu, is read from it.",
     "Tu propia copia del juego. Todo lo del juego, incluidos el arte y la música de este menú, "
     "se lee de ella.",
     "Sua própria cópia do jogo. Tudo do jogo, incluindo a arte e a música deste menu, é lido "
     "dela."},
    {"Credits", "Créditos", "Créditos"},
    {"Built on ReXGlue, the recompiler and runtime that turn the Xbox 360 code into C++ and run "
     "it.",
     "Hecho con ReXGlue, el recompilador y runtime que convierten el código de Xbox 360 en C++ y "
     "lo ejecutan.",
     "Feito com o ReXGlue, o recompilador e runtime que transformam o código do Xbox 360 em C++ "
     "e o executam."},
    {"Xenia, the Xbox 360 emulator whose research, GPU and kernel code ReXGlue builds on.",
     "Xenia, el emulador de Xbox 360 en cuya investigación y código de GPU y kernel se basa "
     "ReXGlue.",
     "Xenia, o emulador de Xbox 360 em cuja pesquisa e código de GPU e kernel o ReXGlue se "
     "baseia."},
    {"FFmpeg decodes the game's XMA audio, Dear ImGui draws this menu, libmspack and stb read the "
     "game's compressed files, and SDL handles controllers.",
     "FFmpeg decodifica el audio XMA del juego, Dear ImGui dibuja este menú, libmspack y stb leen "
     "los archivos comprimidos del juego, y SDL maneja los controles.",
     "O FFmpeg decodifica o áudio XMA do jogo, o Dear ImGui desenha este menu, o libmspack e o "
     "stb leem os arquivos compactados do jogo, e o SDL cuida dos controles."},
    {"Legal", "Legal", "Legal"},
    {"Not affiliated with or endorsed by Electronic Arts, EA Sports, FIFA or Microsoft. 2014 FIFA "
     "World Cup Brazil and its art, music and names belong to their owners.",
     "Sin afiliación ni respaldo de Electronic Arts, EA Sports, FIFA o Microsoft. 2014 FIFA World "
     "Cup Brazil y su arte, música y nombres pertenecen a sus dueños.",
     "Sem afiliação nem apoio da Electronic Arts, EA Sports, FIFA ou Microsoft. 2014 FIFA World "
     "Cup Brazil e sua arte, música e nomes pertencem aos seus donos."},
    {"None of the game's files are included. Its art, sound and data come from your own copy, "
     "and its code is included only in recompiled form. If a rights holder raises a valid legal "
     "concern, it will be addressed.",
     "No se incluye ningún archivo del juego. Su arte, sonido y datos vienen de tu propia copia, "
     "y su código solo se incluye en forma recompilada. Si un titular de derechos plantea una "
     "inquietud legal válida, será atendida.",
     "Nenhum arquivo do jogo está incluído. A arte, o som e os dados vêm da sua própria cópia, e "
     "o código só está incluído em forma recompilada. Se um detentor de direitos levantar uma "
     "questão legal válida, ela será atendida."},
    {"This project's code is under the BSD 3-Clause licence (LICENSE.txt); the components it "
     "uses keep theirs (THIRD-PARTY-NOTICES.txt, both in the program's folder).",
     "El código de este proyecto está bajo la licencia BSD de 3 cláusulas (LICENSE.txt); los "
     "componentes que usa mantienen las suyas (THIRD-PARTY-NOTICES.txt, ambos en la carpeta del "
     "programa).",
     "O código deste projeto está sob a licença BSD de 3 cláusulas (LICENSE.txt); os componentes "
     "que ele usa mantêm as suas (THIRD-PARTY-NOTICES.txt, ambos na pasta do programa)."},
    {"Build", "Versión", "Versão"},
    {"Mention this when reporting a problem, so it's clear which version you're running.",
     "Menciónalo al reportar un problema, para que quede claro qué versión estás usando.",
     "Informe isto ao relatar um problema, para ficar claro qual versão você está usando."},
    {"Version {0}", "Versión {0}", "Versão {0}"},

    // The first start's welcome.
    {"Before you play", "Antes de jugar", "Antes de jogar"},
    {"An unofficial PC version of 2014 FIFA World Cup Brazil, made by recompiling the Xbox 360 "
     "game. It runs on your own copy of the game; nothing of the game's is included.",
     "Una versión no oficial para PC de 2014 FIFA World Cup Brazil, hecha recompilando el juego "
     "de Xbox 360. Funciona con tu propia copia del juego; no incluye nada del juego.",
     "Uma versão não oficial para PC do 2014 FIFA World Cup Brazil, feita recompilando o jogo de "
     "Xbox 360. Ela roda com a sua própria cópia do jogo; nada do jogo está incluído."},
    {"A controller is recommended (Xbox, PlayStation and others work). The keyboard works too, "
     "and its keys are on the Controls page.",
     "Se recomienda un control (funcionan los de Xbox, PlayStation y otros). El teclado también "
     "funciona, y sus teclas están en la página Controles.",
     "É recomendado usar um controle (Xbox, PlayStation e outros funcionam). O teclado também "
     "funciona, e as teclas estão na página Controles."},
    {"Settings here change how the game looks and runs, from the next Play. Show FPS (Settings, "
     "Display) puts the frame rate in the game's corner.",
     "Los ajustes de aquí cambian cómo se ve y funciona el juego, a partir de la próxima vez que "
     "elijas Jugar. "
     "Mostrar FPS (Ajustes, Pantalla) muestra los FPS en una esquina del juego.",
     "As configurações daqui mudam como o jogo fica e roda, a partir da próxima vez que você "
     "escolher Jogar. Mostrar FPS (Configurações, Tela) mostra os FPS no canto do jogo."},
    {"Got it", "Entendido", "Entendi"},

    // Updates.
    {"Update available", "Actualización disponible", "Atualização disponível"},
    {"Version {0}   \xC2\xB7   you have {1}", "Versión {0}   \xC2\xB7   tienes la {1}",
     "Versão {0}   \xC2\xB7   você tem a {1}"},
    {"+ {0} more on the release page", "+ {0} más en la página de la versión",
     "+ {0} mais na página da versão"},
    {"Try again", "Reintentar", "Tentar de novo"},
    {"Update now", "Actualizar ahora", "Atualizar agora"},
    {"Downloading   {0} of {1} MB", "Descargando   {0} de {1} MB", "Baixando   {0} de {1} MB"},
    {"Downloading   {0} MB", "Descargando   {0} MB", "Baixando   {0} MB"},
    {"Checking the download", "Comprobando la descarga", "Verificando o download"},
    {"Installing", "Instalando", "Instalando"},
    {"Starting the update", "Iniciando la actualización", "Iniciando a atualização"},
    {"This release has no download for this kind of copy.",
     "Esta versión no tiene descarga para este tipo de copia.",
     "Esta versão não tem download para este tipo de cópia."},
    {"Couldn't download the checksums.", "No se pudieron descargar las sumas de verificación.",
     "Não foi possível baixar as somas de verificação."},
    {"The download failed. Check your connection and try again.",
     "La descarga falló. Revisa tu conexión e inténtalo de nuevo.",
     "O download falhou. Verifique sua conexão e tente de novo."},
    {"The download didn't match its checksum, so nothing was changed.",
     "La descarga no coincidió con su suma de verificación, así que no se cambió nada.",
     "O download não bateu com a soma de verificação, então nada foi alterado."},
    {"Couldn't unpack the update.", "No se pudo descomprimir la actualización.",
     "Não foi possível descompactar a atualização."},
    {"The update has no launcher in it.", "La actualización no contiene el launcher.",
     "A atualização não contém o launcher."},
    {"Couldn't replace {0}. Is the game running?",
     "No se pudo reemplazar {0}. ¿Está abierto el juego?",
     "Não foi possível substituir {0}. O jogo está aberto?"},
    {"Couldn't write {0} into the game's folder.",
     "No se pudo escribir {0} en la carpeta del juego.",
     "Não foi possível gravar {0} na pasta do jogo."},
    {"The update was cancelled at Windows' prompt.",
     "La actualización se canceló en el aviso de Windows.",
     "A atualização foi cancelada no aviso do Windows."},
    {"Couldn't start the installer.", "No se pudo iniciar el instalador.",
     "Não foi possível iniciar o instalador."},
    {"The update is in place, but the new launcher didn't start. Start it again yourself.",
     "La actualización está instalada, pero el nuevo launcher no se abrió. Ábrelo tú de nuevo.",
     "A atualização foi instalada, mas o novo launcher não abriu. Abra-o novamente."},

    // Starting the game.
    {"The game (fifawc14.exe) is missing from the launcher's folder. Your antivirus may have "
     "removed it. Restore it there, or download the release again.",
     "Falta el juego (fifawc14.exe) en la carpeta del launcher. Puede que tu antivirus lo haya "
     "eliminado. Restáuralo ahí o vuelve a descargar la versión.",
     "O jogo (fifawc14.exe) não está na pasta do launcher. Talvez o seu antivírus o tenha "
     "removido. Restaure-o nessa pasta ou baixe a versão de novo."},
    {"The game (fifawc14.exe) couldn't be started (Windows error {0}). Your antivirus may be "
     "blocking it.",
     "No se pudo iniciar el juego (fifawc14.exe) (error de Windows {0}). Puede que tu antivirus "
     "lo esté bloqueando.",
     "Não foi possível iniciar o jogo (fifawc14.exe) (erro do Windows {0}). Talvez o seu "
     "antivírus esteja bloqueando."},
    {"The game stopped: your processor doesn't support SSE4.2, which the game needs (Intel from "
     "2008 on, AMD from 2011).",
     "El juego se detuvo: tu procesador no es compatible con SSE4.2, que el juego necesita (Intel "
     "desde 2008, AMD desde 2011).",
     "O jogo parou: seu processador não é compatível com SSE4.2, que o jogo precisa (Intel a "
     "partir de 2008, AMD a partir de 2011)."},
    {"The game couldn't read where the game files are from fifawc14.toml in {0}. Pick the game "
     "again in Settings, Data, Game.",
     "El juego no pudo leer dónde están sus archivos desde fifawc14.toml en {0}. Vuelve a elegir "
     "el juego en Ajustes, Datos, Juego.",
     "O jogo não conseguiu ler onde estão os arquivos do jogo a partir do fifawc14.toml em {0}. "
     "Escolha o jogo de novo em Configurações, Dados, Jogo."},
    {"The game's location couldn't be saved to fifawc14.toml in {0}, so the game won't find it. "
     "Windows' Controlled folder access or your antivirus may be blocking the launcher from "
     "writing there.",
     "No se pudo guardar la ubicación del juego en fifawc14.toml en {0}, así que el juego no la "
     "encontrará. El acceso controlado a carpetas de Windows o tu antivirus pueden estar "
     "impidiendo que el launcher escriba ahí.",
     "Não foi possível salvar o local do jogo no fifawc14.toml em {0}, então o jogo não vai "
     "encontrá-lo. O acesso controlado a pastas do Windows ou seu antivírus podem estar "
     "impedindo o launcher de gravar ali."},
    {"The game closed unexpectedly (error {0}). Please report it with fifawc14.log (Settings, "
     "Data, Logs opens its folder).",
     "El juego se cerró inesperadamente (error {0}). Repórtalo junto con fifawc14.log (Ajustes, "
     "Datos, Registros abre su carpeta).",
     "O jogo fechou inesperadamente (erro {0}). Relate o problema com o fifawc14.log "
     "(Configurações, Dados, Registros abre a pasta dele)."},
    {"The menu's art couldn't be read from the game's files.",
     "No se pudo leer el arte del menú desde los archivos del juego.",
     "Não foi possível ler a arte do menu nos arquivos do jogo."},
    {"Windows' Segoe UI font is missing.", "Falta la fuente Segoe UI de Windows.",
     "A fonte Segoe UI do Windows está faltando."},

    // Finding the game (setup).
    {"ONE LAST STEP", "UN ÚLTIMO PASO", "SÓ MAIS UM PASSO"},
    {"Show us where your game is", "Muéstranos dónde está tu juego", "Mostre onde está o seu jogo"},
    {"The game isn't included. Use your own copy of 2014 FIFA World Cup Brazil for Xbox 360.",
     "El juego no está incluido. Usa tu propia copia de 2014 FIFA World Cup Brazil para Xbox 360.",
     "O jogo não está incluído. Use a sua própria cópia do 2014 FIFA World Cup Brazil para Xbox "
     "360."},
    {"Drop your .iso or game folder here", "Suelta aquí tu .iso o la carpeta del juego",
     "Solte aqui o seu .iso ou a pasta do jogo"},
    {"Drop it here", "Suéltalo aquí", "Solte aqui"},
    {"or", "o", "ou"},
    {"Choose .iso file", "Elegir archivo .iso", "Escolher arquivo .iso"},
    {"Choose folder", "Elegir carpeta", "Escolher pasta"},
    {"GAME FOUND", "JUEGO ENCONTRADO", "JOGO ENCONTRADO"},
    {"Ready to play", "Listo para jugar", "Pronto para jogar"},
    {"European disc", "Disco europeo", "Disco europeu"},
    {"US disc", "Disco de EE. UU.", "Disco dos EUA"},
    {"Use a different copy", "Usar otra copia", "Usar outra cópia"},
    {"Change where the game is", "Cambiar dónde está el juego", "Mudar onde está o jogo"},
    {"It's read from {0} now.", "Ahora se lee desde {0}.", "Agora é lido de {0}."},
    {"No game in that folder", "No hay ningún juego en esa carpeta", "Não há jogo nessa pasta"},
    {"Choose the folder that has default.xex in it, or the game's .iso.",
     "Elige la carpeta que contiene default.xex, o el .iso del juego.",
     "Escolha a pasta que contém o default.xex, ou o .iso do jogo."},
    {"That's not a game folder or .iso", "Eso no es una carpeta de juego ni un .iso",
     "Isso não é uma pasta de jogo nem um .iso"},
    {"Choose the game's .iso, or its folder (the one with default.xex).",
     "Elige el .iso del juego o su carpeta (la que tiene default.xex).",
     "Escolha o .iso do jogo ou a pasta dele (a que tem o default.xex)."},
    {"That's a different game", "Ese es otro juego", "Esse é outro jogo"},
    {"It's an Xbox 360 game, but not 2014 FIFA World Cup Brazil. Choose your copy of 2014 FIFA "
     "World Cup Brazil.",
     "Es un juego de Xbox 360, pero no es 2014 FIFA World Cup Brazil. Elige tu copia de 2014 "
     "FIFA World Cup Brazil.",
     "É um jogo de Xbox 360, mas não é o 2014 FIFA World Cup Brazil. Escolha a sua cópia do 2014 "
     "FIFA World Cup Brazil."},
    {"This copy can't be used", "Esta copia no se puede usar", "Esta cópia não pode ser usada"},
    {"It's 2014 FIFA World Cup Brazil, but another version, or patched or modded. Use the "
     "European or US disc.",
     "Es 2014 FIFA World Cup Brazil, pero de otra versión, o con parches o mods. Usa el disco "
     "europeo o el de EE. UU.",
     "É o 2014 FIFA World Cup Brazil, mas de outra versão, ou com patch ou mod. Use o disco "
     "europeu ou o dos EUA."},
    {"Part of the game is missing", "Falta una parte del juego", "Falta uma parte do jogo"},
    {"{0} isn't there. Copy the whole disc, or use its .iso.",
     "Falta {0}. Copia el disco completo o usa su .iso.",
     "{0} não está lá. Copie o disco inteiro ou use o .iso dele."},
    {"Ported by hosse ({0}). Unofficial, and not affiliated with or endorsed by EA, FIFA or "
     "Microsoft.",
     "Port hecho por hosse ({0}). No oficial, sin afiliación ni respaldo de EA, FIFA o "
     "Microsoft.",
     "Port feito por hosse ({0}). Não oficial, sem afiliação nem apoio da EA, FIFA ou "
     "Microsoft."},
    {"The game's disc image", "La imagen de disco del juego", "A imagem de disco do jogo"},
    {"The game's folder (with default.xex)", "La carpeta del juego (con default.xex)",
     "A pasta do jogo (com o default.xex)"},
    {"Disc images (*.iso)", "Imágenes de disco (*.iso)", "Imagens de disco (*.iso)"},
    {"All files", "Todos los archivos", "Todos os arquivos"},
};

const std::vector<LanguageInfo> kLanguages = {
    {Language::kEnglish, "en", "English", 1},
    {Language::kSpanish, "es", "Español", 5},
    {Language::kPortuguese, "pt", "Português", 9},
};

std::atomic<Language> g_language{Language::kEnglish};

const std::unordered_map<std::string_view, const Entry*>& Table() {
  static const auto table = [] {
    std::unordered_map<std::string_view, const Entry*> map;
    for (const Entry& entry : kEntries) map.emplace(entry.en, &entry);
    return map;
  }();
  return table;
}

}  // namespace

const std::vector<LanguageInfo>& Languages() { return kLanguages; }

const LanguageInfo& Info(Language language) {
  for (const LanguageInfo& info : kLanguages) {
    if (info.id == language) return info;
  }
  return kLanguages[0];
}

Language CurrentLanguage() { return g_language.load(); }
void SetLanguage(Language language) { g_language = language; }

Language LanguageFromCode(std::string_view code, Language fallback) {
  for (const LanguageInfo& info : kLanguages) {
    if (code == info.code) return info.id;
  }
  return fallback;
}

const char* T(const char* english) {
  Language language = g_language.load();
  if (language == Language::kEnglish) return english;
  auto it = Table().find(english);
  if (it == Table().end()) return english;
  return language == Language::kSpanish ? it->second->es : it->second->pt;
}

std::string Tf(const char* english, std::initializer_list<std::string_view> args) {
  std::string text = T(english);
  size_t index = 0;
  for (std::string_view arg : args) {
    std::string mark = "{" + std::to_string(index++) + "}";
    if (size_t at = text.find(mark); at != std::string::npos) text.replace(at, mark.size(), arg);
  }
  return text;
}

}  // namespace launcher
