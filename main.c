#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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
	size_t bytes_read = 0;
	size_t global_offset = 0; // Acumulador total de bytes leidos en el disco, para saber con exactitud donde ha caido la firma(bytes totales) 
	size_t block_count = 0; // Contador para saber en que bloque fisico estamos

	
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

	// El bucle lee bloques de 4096 elementos, de 1 byte cada uno
	while ((bytes_read = fread(buffer, 1, sizeof(buffer), disk_image)) > 0) {
		
		// Dentro de este while, lo primero que evaluamos es el estado actual: 
		if (carving == 1) {
			// Estamos en el modo extracción.
			// Guardamos de forma consecutiva el bloque actual en el archivo recuperado
			fwrite(buffer, 1, bytes_read, output_file);
		}

		// *Bloque añadido
		// Frontera entre bloques
		// Si NO es la primera vuelta del disco, comprobamos si el último byte del bloque anterior era 0x1F
		// Y el priemr byte de este nuevo bloque es 0x8B
		if (!first_iteration)  {
			if (last_byte == 0x1F && buffer[0] == 0x8B) {
				// El 0x1F estaba en el ultimo byte del bloque anterior.
				// Por lo tanto, la firma empieza exactamente en (global_offset -1)
				printf("[+] Gzip signature found exactly on the block boundary!\n");
				printf("	-> Absolute disk offset: %zu (0x%zX)\n", global_offset - 1, global_offset - 1);
			}
		}

		first_iteration = 0; // Ya pasamos la primera iteración, "apagamos el interruptor"
		

		// Bucle secundario: Analizamos el búfer actual byte a byte 
		// Recorremos desde la posición 0 hasta (bytes_read -1).
		// Restamos 1 porque dentro comprobamos '1' e 'i+1'. Si estuviéramos en el último byte,
		// 'i+1' se saldría del búfer
		for (size_t i = 0; i < bytes_read - 1; i++) {
			
			// Si el byte actual es 0x1F y el siguiente es 0x8B lo hemos encontrado
			// (Aunque hara falta hacer mas comprobaciones para evitar una 
			// posible coincidencia por probabilidad en discos duros grandes,
            // teniendo que comprobar el byte 3 qu es el método de compresión y
            // el byte 4 que serían las flags (FNAME por ejemplo)).
			if (buffer[i] == 0x1F && buffer[i+1] == 0x8B) {
				// Sumamos el global_offset (bytes de bloques pasados) + i (posicion actual en este bloque)
				size_t absolute_address = global_offset + i;
				carving = 1;
				file_count++;

				// Ahora abrimos el archivo de salida en modo escritura binaria.
				output_file = fopen("recovered_file.gz", "wb"); // (veremos luego como automatizarlo, por el momento tiene un nombre fijo temporalmente). 
				
				// Ponemos un control de seguridad: Si el sistema no nos deja
				// crear el archivo, abortamos
				if (output_file == NULL) {
					fprintf(stderr, "[-] Error: cannot create recovery file.\n");
					return EXIT_FAILURE;
				}

				size_t bytes_to_save = bytes_read -i;
				fwrite(&buffer[i], 1, bytes_to_save, output_file);


				printf("[+] Gzip signature detected in block %zu (buffer index: %zu).\n", block_count, i);
				printf("	-> Absolute disk offset: %zu (0x%zX)\n", absolute_address, absolute_address);
				

			}
		}

		// Antes de que termine el while y se destruya el búfer actual para leer el siguiente, 
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
