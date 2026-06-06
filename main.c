/* Esta macro debe ir estrictamente en la línea 1, antes de cualquier #include.
 * Al hacerlo, el preprocesador activa la configuración de 64 bits antes de que 
 * se carguen las librerías del sistema. Esto garantiza que funciones como ftello() y el tipo off_t
 * utilicen siempre cajas de 64 bits para soportar archivos gigantes, incluso si el programa se
 * ejecuta en arquitecturas de 32 bits. La barra baja del principio es obligatoria e indica que es 
 * una variable de control interna del sistema
 */
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>


int main (int argc, char *argv[]) {
  	
  // 1: Declaramos el puntero al archivo para manejo con búfer (stream)
  FILE *disk_image = NULL;

  // Si el usuario escribe mas de un argumento en la terminal, avisamos del error
  if (argc != 2) {
      fprintf(stderr, "[-] Error: Invalid arguments.\n");
      fprintf(stderr, "[-] Usage: flcarve <disk_image> or use -h/--help to open the help panel\n");
      return EXIT_FAILURE;
  }

  // Si introduce exactamente un argumento (argc == 2)
  // Comprobamos si solicita el panel de ayuda
  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      printf("\n=== flcarve Help Panel ===\n");
      printf("A low-level DFIR utility designed to carve and reconstruct unlinked .gz files\n");
      printf("by performing direct signature scanning over raw disk images.\n");
      printf("Usage:\n");
      printf("\tflcarve <disk_image>\n\n");
      printf("Options:\n");
      printf("\t-h, --help\t\tShow help panel.\n");
      return EXIT_SUCCESS; // Terminamos con éxito tras mostrar la ayuda
  }

  // Si el argumento no era "-h", asumimos que argv[1] es la ruta de la imagen de disco
  printf("[+] Target disk image received: %s\n", argv[1]);

  
  // ***El siguiente paso logico sera abrir el archivo aqui


  // Intentamos abrir la imagen de disco en modo Lectura Binaria ("rb")
  disk_image = fopen(argv[1], "rb");
  
  // Si fopen devuelve NULL, significa que el archivo no existe o faltan permisos de administrador
  if (disk_image == NULL) {
      fprintf(stderr, "[-] Error: cannot open target disk image '%s'.\n", argv[1]);
      fprintf(stderr, "[-] Check if the file path is correct and ensure you have root privileges.\n");
      return EXIT_FAILURE;
  }

  printf("[+] Successfully opened disk image in binary read mode.\n");

  // ***Aqui ira el bucle de lectura

  // Declaramos el bufer de 4096 bytes en el stack
  unsigned char buffer[4096];
  
  // Por qué size_t para estas variables?
  /* size_t no es un tipo de dato como int o char que quizá son mas primitivos. Es un alias (un typedef) que el sistema operativo define
   * para representar el tamaño de cualquier objeto en memoria o el índice máximo de un array.
   * La característica principal es que e smultiplataforma y se adapta a la arquitectura de
   * la CPU. En un sistema de 32 bits, size_t ocupa 4 bytes (32bits), mientras que en un sistema
   * moderno ocupa 8 bytes (64bits). size_t, que no tiene signo y usa los 64 bits enteros, puedes
   * analziar hasta 16 exabytes (18.4 trillones de bytes), haciendo que puedas meterle imágenes de disco de un tamaño enorme sin miedo. 
   */ 
  size_t bytes_read = 0;
  size_t global_offset = 0; // Acumulador total de bytes leidos en el disco, para saber con exactitud donde ha caido la firma(bytes totales) 
  size_t block_count = 0; // Contador para saber en que bloque fisico estamos

  
  unsigned char penultimate_byte = 0x00;
  // Variable externa para recordar el último byte del bloque anterior
  // Usamos unsigned char (1 byte, de 0 a 255) igual que el búfer
  unsigned char last_byte = 0x00;
  int first_iteration = 1; // un interruptor para saber si es el primer bloque de todos
  
  // Variables de control para la máquina de estados
  // 'carving' actúa como el interruptor principal de nuestra máquina de estados.
  // 0 = Modo escaneo: el programa buscará los magic bytes de gzip (0x1F 0x8B 0x08...)
  // 1 = Modo extracción: Habremos cruzado una cabecera válida y estaremos volcando los bloques de datos
  // directamente al archivo de salida (.gz)
  int carving = 0;

  // Es un descriptor de archivo (stream) para el archivo gzip que estamos reconstruyendo.
  // Permanecerá en NULL mientras 'carving == 0', y apuntará a un archivo abierto 
  // en modo escritura binaria (wb) cuándo localicemos una firma en el disco
  FILE *output_file = NULL; // recordemos que a las variables hay que asignarles un valor porque por defecto contienen basura
  
  // Esta variable sera el contador incremental para la nomenclatura de los archivos recuperados.
  // (Es probable que encontremos más de un gzip)
  // Esto nos permitirá generar nombres únicos de forma secuencial cada vez
  // que la máquina de estados detecte un nuevo log. 
  size_t file_count = 0; 

  // ** añadido: Esto es una variable de control que marca el índice de inicio de escritura
  // dentro del buffer de 4096 bytes.
  // Se reinicia a 0 en cada bloque limpio para volcarlo entero (cuando volquemos comprimidos grandes
  // y entre medias no hayamos encontrado ninguna otra firma), y se actualiza al índice 'i'
  // cuando encontramos una firma para realizar los cortes correctamente entre los archivos 
  size_t write_start = 0;

  // El bucle lee bloques de 4096 elementos, de 1 byte cada uno
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), disk_image)) > 0) {
    
    // Reiniciamos en cada vuelta 
    write_start = 0; 
    


    // *Bloque añadido
    // Frontera entre bloques
    // Si NO es la primera vuelta del disco, comprobamos si el último byte del bloque anterior era 0x1F
    // Y el priemr byte de este nuevo bloque es 0x8B
    if (!first_iteration)  {

      // Bloque añadido: nueva comprobación con la misma lógica.
      if (penultimate_byte == 0x1F && last_byte == 0x8B && buffer[0] == 0x08) {
        printf("[+] Gzip signature found on the block boundary!\n");
        printf("	Absolute disk offset: %zu (0x%zX)\n", global_offset -2, global_offset - 2); // Quiero mencionar que el especificador de formato zu quiere decir esto:
                                                            // z: le dice a la función print que tamaño del argumento corresponde a size_t (que cambia según la arquitectura del procesador. En sistemas de 64 bits suele ocupar 8 bytes).
                                                            // u: es de unsigned ya que las posiciones de memoria y los contadores de bytes nunca pueden ser negativos.
                                                            // Y zX nos indica con la X mayúscula que pinte las letras del hexadecimal en mayúscula (y que es hexadecimal como tal). 
        
        if (carving == 1 && output_file != NULL) {
          /* Antes de hacer fflush(), los datos estan en el 'Búfer de Usuario'. Es una zona de la RAM
           * privada que pertenece al programa y que gestiona la librería estandar de C (glibc). 
           * El kernel no sabe que esos bytes existen todavía. Cuando ejecutamos fflush(), el programa
           * realiza una syscall. Los datos se copian de la zona privada del programa a la 
           * Page Cache (caché de páginas). Esta es otra zona de la RAM, pero está gestionada 
           * directamente por el kernel. Esto lo digo porque ftruncate() es una función nativa de
           * linux que habla directamente al kernel. Por eso necesita que los datos
           * ya estén en la Page Cache del kernel para saber que bytes tiene que recortar. Si se 
           * quedaran en el bufer de usuario, no se podria aplicar el corte correctamente. 
           */
          fflush(output_file);
          
          /* ftell() es una función clásica de C. Tú le pasas un archivo y la función te devuelve
           * la posición actual del cursor (cuántos bytes llevamos escritos). El problema de ftell()
           * es que está programada para devolver un tipo de dato 'long', pero en muchos sistemas
           * el long se queda corto (máximo 2GB en arquitecturas de 32 bits). 
           * Si el archivo recuperado creciera más de esa cifra,
           * ftell() se desbordaría y daría un dato falso. Por eso usamos ftello(), es la misma
           * función (la o viene de offset) pero devuelve el tipo off_t (64 bits)
           * El int normal ocupa 4 bytes. Al usar off_t, el sistema operativo adapta automáticamente
           * el tamaño de la caja según tu arquitectura. En el linux moderno off_t ocupa 8 bytes.
           * */
          off_t file_size = ftello(output_file);

          /* Con las notas y líneas de cóiigo anteriores, nos aseguramos que los bytes estén en 
           * posesión del kernel. Cómo el kernel tiene ahora los bytes, podemos pasarle el ftello
           * pasa saber cuando mide el archivo entero actualmente. El resultado lo guardamos en la 
           * variable para poder usar ftruncate() de manera óptima. 
           * ftruncate() es una syscall, que modifica físicamente el tamaño del archivo en el disco.
           * Necesita el descriptor de archivo (igual que todas las syscalls, por eso le pasamos
           * el fileno(), no maneja flujos (streams), que son de más alta abstracción. 
           * Dejamos el archivo truncado para que no se guarden esos dos últimos bytes ni se queden
           * huérfanos. 
           *
           */
          ftruncate(fileno(output_file), file_size - 2);
          fclose(output_file);
        // Ahora, tenemos que activar el interruptor de extracción e incrementar el contador de archivos
        }

        file_count++;
        carving = 1;
        // También debemos marcar el nuevo inicio de escritura antes de asegurar los bytes huérfanos,
        // Lo que le indicamos en este caso es que el tercer byte de la firma (el 0x08), ha caído
        // en el índice 0 de nuestro array 'buffer' actual. Como lo igualamos a 0, le decimos
        // al programa que termine, escriba todo el buffer desde el byte 0 en adelante
        write_start = 0;


        // tenemos el paso abierto para poder abrir el nuevo archivo con fopen 
        // y luego meter mano a los dos bytes
        // que se habían quedado colgando (huérfanos). Pero como la creación de filename y el snprintf
        // ocurren dentro del bucle for secundario, aqui no existe todavía. 
        char filename[64];
        snprintf(filename, sizeof(filename), "recovered_log_%zu.gz", file_count);

        output_file = fopen(filename, "wb");
        if (output_file == NULL) {
          fprintf(stderr, "[-] Error: cannot create recovery file %s.\n", filename);
          return EXIT_FAILURE;
        }
        fwrite(&penultimate_byte, 1, 1, output_file); // Corresponderia a 0x1F
        fwrite(&last_byte, 1, 1, output_file);
        /* Quiero apuntar algo. En C, cuando pasamos una  variable normal a una función 
         * (como un número entero o un caracter), la función recibe una copia de ese valor. 
         * Fwrite es una función de bajo nivel diseñada para el rendimiento. Su trabajo es coger datos
         * de la RAM de nuestro programa y moverlos al bufer interno de la libreria de C
         * (otra zona de la RAM). Se quedan ahí acumulados en ese bufer para no saturar el disco
         * duro con micro escrituras, delegando el volcado real al sistema operativo mediante
         * funciones como fflush() o al cerrar el archivo. 
         * Fwrite necesita obligatoriamente una dirección de memoria de la RAM. El & es el puntero
         * que le pasamos para indicarle donde está esa dirección. 
         * Con esa dirección, fwrite va a donde le has marcado, coge los bytes
         * pedidos y los transfiere al bufer intermedio de la librería de C en la RAM, esperando
         * a ser vaciado hacia el sistema operativo.
         * last_byte sin el puntero haría que la función recibiera el número 139 (que en decimal
         * es el 0x8B), lo cual haría que saltara a una dirección prohibida
        

*/
      }


      if (last_byte == 0x1F && buffer[0] == 0x8B && buffer[1] == 0x08) { // ** añadido la tercera firma, ahora hace falta añadir un bloque nuevo con una nueva variable donde
                                                                         // podamos guardar el penúltimo byte, que se plantean nuevos peligros 
        if (carving == 1 && output_file != NULL) {           
          fflush(output_file);
          off_t file_size = ftello(output_file);
          ftruncate(fileno(output_file), file_size - 1); // Misma lógica que en aquel bloque anterior
          fclose(output_file);
        }

        file_count++;
        carving = 1;
        write_start = 0;
        char filename[64];
        snprintf(filename, sizeof(filename), "recovered_log_%zu.gz", file_count);
        output_file = fopen(filename, "wb");
        
        if (output_file == NULL) {
          fprintf(stderr, "[-] Error: cannot create a recovery file %s.\n", filename);
          return EXIT_FAILURE;
        }

        fwrite(&last_byte, 1, 1, output_file);

        // El 0x1F estaba en el ultimo byte del bloque anterior.
        // Por lo tanto, la firma empieza exactamente en (global_offset -1)
        printf("[+] Gzip signature found exactly on the block boundary!\n");
        printf("	-> Absolute disk offset: %zu (0x%zX)\n", global_offset - 1, global_offset - 1);
        
        // ***Aqui iría una una lógica similar para cerrar el archivo anterior 
        // y abrir el nuevo recovered_log_x.gzi
      }
    }

    first_iteration = 0; // Ya pasamos la primera iteración, "apagamos el interruptor"
      

    // Bucle secundario: Analizamos el búfer actual byte a byte 
    // Recorremos desde la posición 0 hasta (bytes_read -1).
    // Restamos 1 porque dentro comprobamos '1' e 'i+1'. Si estuviéramos en el último byte,
    // 'i+1' se saldría del búfer
    // ***** Añadido: Como ahora tenemos que comprobar tambien el tercer byte, le tenemos que sumar dos para que el maximo valor de i sea 4093 y no 4094.
    // No queremos salirnos de la memoria. 
    for (size_t i = 0; i < bytes_read - 2; i++) { // Aquí el problema de que i < bytes_read -2 siempre sea 4094(4093) y nunca lleguemos a los dos bytes del final, por lo que hay que poner una nueva lógica
      
      
  
      // Si el byte actual es 0x1F y el siguiente es 0x8B lo hemos encontrado
      // (Aunque hará falta hacer más comprobaciones para evitar una 
      // posible coincidencia por probabilidad en discos duros grandes,
      // teniendo que comprobar el byte 3 que es el método de compresión y
      // el byte 4 que serían las flags (FNAME por ejemplo)).
      if (buffer[i] == 0x1F && buffer[i+1] == 0x8B && buffer[i+2] == 0x08) {

        // Si estábamos extrayendo un archivo antes, lo cerramos para no dejar descriptores abiertos
        if (carving == 1 && output_file != NULL) {
          fwrite(&buffer[write_start], 1, i - write_start, output_file); // Salvamos el fragmento que le correspondía al archivo anterior desde donde
                                                                         // empezó en este bloque (write_start) hasta
                                                                         // justo antes de la nueva firma (i).
                                                                         // Esto evitará duplicados si nos encontramos varias firmas en un bloque
                                                                  
          fclose(output_file);
          printf("[+] Previous file closed successfully to prevent data mixing.\n");
        }
        
        // Activamos el estado de extracción e incrementamos el contador
        carving = 1;
        file_count++;

        // Actualizamos write_start para guardar la posición del byte donde haya caido la firma
        // antes de que el bucle for termine y destruya la variable i. 
        write_start = i;
          
        // Creamos un nombre de archivo dinámico basado en el contador
        char filename[64]; // Es un array para almacenar el texto "recovered_log_x.gz"
        snprintf(filename, sizeof(filename), "recovered_log_%zu.gz", file_count);


        // Ahora abrimos el archivo de salida en modo escritura binaria.
        output_file = fopen(filename, "wb");  
          
        // Ponemos un control de seguridad: Si el sistema no nos deja
        // crear el archivo, abortamos
        if (output_file == NULL) {
          fprintf(stderr, "[-] Error: cannot create recovery file %s.\n", filename);
          return EXIT_FAILURE;
        }
          
        // Calculamos la dirección absoluta en el disco crudo
        size_t absolute_address = global_offset + i;

        printf("[+] Gzip signature detected in block %zu (buffer index: %zu).\n", block_count, i);
        printf("	-> Absolute disk offset: %zu (0x%zX)\n", absolute_address, absolute_address);
        printf("	-> Extracting data into target stream: %s\n", filename);

      }
    }

    // Bloque añadido: el for no necesita llegar al final. Él se encarga del centro del bloque, y 
    // esta nueva lógica son los if de frontera que estan fuera y se encarga de los bytes que quedaron
    // en los bordes

    if (carving == 1 && output_file != NULL) {
      fwrite(&buffer[write_start], 1, bytes_read - write_start, output_file);
      /* Por que &buffer[write_start]? Porque si hubo una firma a mitad del bloque, write_start 
       * apuntara a i, escribiendo solo el fragmento restante (bytes_read - write_start). Si el bloque
       * estuviera limpio de firmas, entonces write_start vale 0 y escribe los 4096 bytes desde el principio, enteros (&buffer[0]). 
       * usamos & porque fwrite necesita una dirección de memoria (un puntero). 
       */ 
    }

    // Antes de que termine el while y se destruya el búfer actual para leer el siguiente, 
      
    // Añadido: guardamos ahora también el penúltimo byte
    penultimate_byte = buffer[bytes_read - 2];
    // guardamos el último byte leído en nuestra variable externa
    last_byte = buffer[bytes_read - 1];

    global_offset += bytes_read; // Sumamos los bytes que acabamos de procesar
    block_count++; // Avanzamos al siguiente bloque
  }

  printf("[+] Finished scanning the disk image. Total bytes scanned: %zu\n", global_offset);

  // Regla de oro en C: todo lo que se abre, se tiene que cerrar
  fclose(disk_image);
  return EXIT_SUCCESS;
}
