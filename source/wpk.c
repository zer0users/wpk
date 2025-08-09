#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <curl/curl.h>
#include <errno.h>

#define MAX_URL_SIZE 512
#define MAX_PACKAGE_NAME 128
#define MAX_BUFFER_SIZE 1024
#define MAX_RESPONSE_SIZE 65536
#define BASE_URL "https://github.com/zer0users/wpk-repositories/raw/refs/heads/main/packages/"
#define API_URL "https://api.github.com/repos/zer0users/wpk-repositories/contents/packages"

// Estructura para manejar la descarga
struct DownloadData {
    FILE *fp;
    long total_size;
    long downloaded;
};

// Estructura para manejar respuestas de API
struct APIResponse {
    char *data;
    size_t size;
};

// Callback para escribir datos descargados
size_t write_callback(void *contents, size_t size, size_t nmemb, struct DownloadData *data) {
    size_t written = fwrite(contents, size, nmemb, data->fp);
    data->downloaded += written;
    return written;
}

// Callback para escribir respuesta de API
size_t api_write_callback(void *contents, size_t size, size_t nmemb, struct APIResponse *response) {
    size_t total_size = size * nmemb;
    size_t new_size = response->size + total_size;
    
    char *new_data = realloc(response->data, new_size + 1);
    if (!new_data) {
        return 0;
    }
    
    response->data = new_data;
    memcpy(response->data + response->size, contents, total_size);
    response->size = new_size;
    response->data[response->size] = '\0';
    
    return total_size;
}

// Callback para obtener el tamaño del archivo
size_t header_callback(char *buffer, size_t size, size_t nitems, long *content_length) {
    if (strncasecmp(buffer, "Content-Length:", 15) == 0) {
        *content_length = atol(buffer + 16);
    }
    return size * nitems;
}

long get_file_size(const char *url) {
    CURL *curl;
    CURLcode res;
    long content_length = 0;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &content_length);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    
    return content_length;
}

int list_packages(void) {
    CURL *curl;
    CURLcode res;
    struct APIResponse response = {0};
    
    printf("Fetching available packages...\n\n");
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Error: Could not initialize curl\n");
        return 1;
    }
    
    // Configurar curl para la API de GitHub
    curl_easy_setopt(curl, CURLOPT_URL, API_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "WPK/1.0");
    
    // Realizar petición
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        printf("Error: Failed to fetch package list: %s\n", curl_easy_strerror(res));
        if (response.data) free(response.data);
        return 1;
    }
    
    if (!response.data) {
        printf("Error: No data received from API\n");
        return 1;
    }
    
    printf("Available packages:\n");
    printf("==================\n");
    
    // Parsear JSON simple para extraer nombres de archivos .wpk
    char *json = response.data;
    char *pos = json;
    int package_count = 0;
    
    while ((pos = strstr(pos, "\"name\":")) != NULL) {
        pos += 7; // Saltar "name":
        
        // Saltar espacios y comillas
        while (*pos == ' ' || *pos == '"') pos++;
        
        // Encontrar el final del nombre
        char *end = pos;
        while (*end && *end != '"') end++;
        
        if (*end == '"') {
            size_t name_len = end - pos;
            char name[256];
            
            if (name_len < sizeof(name)) {
                strncpy(name, pos, name_len);
                name[name_len] = '\0';
                
                // Verificar si es un archivo .wpk
                if (strlen(name) > 4 && strcmp(name + strlen(name) - 4, ".wpk") == 0) {
                    // Remover la extensión .wpk
                    name[strlen(name) - 4] = '\0';
                    printf("  %s\n", name);
                    package_count++;
                }
            }
        }
        
        pos = end;
    }
    
    printf("==================\n");
    printf("Total packages: %d\n", package_count);
    
    if (package_count == 0) {
        printf("No packages found or unable to parse response.\n");
    }
    
    free(response.data);
    return 0;
}

int download_package(const char *package_name, const char *output_file) {
    CURL *curl;
    CURLcode res;
    char url[MAX_URL_SIZE];
    struct DownloadData data;
    
    // Construir URL
    snprintf(url, sizeof(url), "%s%s.wpk", BASE_URL, package_name);
    
    printf("Checking information..\n");
    
    // Obtener tamaño del archivo
    long file_size = get_file_size(url);
    if (file_size <= 0) {
        printf("Error: Package '%s' not found or size could not be determined\n", package_name);
        return 1;
    }
    
    // Mostrar información del paquete
    printf("=======%s=======\n", package_name);
    printf("This package is %ld Bytes, Do you want to continue? (Y/N): ", file_size);
    
    char response;
    scanf(" %c", &response);
    
    if (response != 'y' && response != 'Y') {
        printf("Installation cancelled.\n");
        return 0;
    }
    
    printf("===================\n");
    
    // Abrir archivo para escritura
    data.fp = fopen(output_file, "wb");
    if (!data.fp) {
        printf("Error: Could not create output file %s\n", output_file);
        return 1;
    }
    
    data.total_size = file_size;
    data.downloaded = 0;
    
    // Inicializar curl
    curl = curl_easy_init();
    if (!curl) {
        printf("Error: Could not initialize curl\n");
        fclose(data.fp);
        return 1;
    }
    
    // Configurar curl
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "WPK/1.0");
    
    // Realizar descarga silenciosamente
    res = curl_easy_perform(curl);
    
    // Limpiar
    curl_easy_cleanup(curl);
    fclose(data.fp);
    
    if (res != CURLE_OK) {
        printf("Error: Download failed: %s\n", curl_easy_strerror(res));
        remove(output_file);
        return 1;
    }
    
    return 0;
}

int extract_wpk(const char *wpk_file, const char *extract_dir) {
    char command[MAX_BUFFER_SIZE];
    int result;
    
    // Crear directorio de extracción
    mkdir(extract_dir, 0755);
    
    // Extraer usando unzip silenciosamente
    snprintf(command, sizeof(command), "unzip -q '%s' -d '%s'", wpk_file, extract_dir);
    result = system(command);
    
    if (result != 0) {
        printf("Error: Failed to extract package\n");
        return 1;
    }
    
    return 0;
}

int check_and_run_packagefile(const char *extract_dir) {
    char packagefile_path[MAX_BUFFER_SIZE];
    char command[MAX_BUFFER_SIZE];
    char find_command[MAX_BUFFER_SIZE];
    struct stat st;
    int result;
    
    // Buscar Packagefile en el directorio extraído y subdirectorios
    snprintf(find_command, sizeof(find_command), "find '%s' -name 'Packagefile' -type f", extract_dir);
    FILE *find_fp = popen(find_command, "r");
    
    if (find_fp == NULL) {
        printf("No Packagefile found, installation complete.\n");
        return 0;
    }
    
    // Leer el resultado del find
    if (fgets(packagefile_path, sizeof(packagefile_path), find_fp) == NULL) {
        printf("No Packagefile found, installation complete.\n");
        pclose(find_fp);
        return 0;
    }
    
    pclose(find_fp);
    
    // Remover salto de línea del final
    packagefile_path[strcspn(packagefile_path, "\n")] = 0;
    
    // Obtener el directorio del Packagefile
    char *packagefile_dir = strdup(packagefile_path);
    char *last_slash = strrchr(packagefile_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    }
    
    // Cambiar al directorio del Packagefile y ejecutarlo
    char original_dir[MAX_BUFFER_SIZE];
    getcwd(original_dir, sizeof(original_dir));
    
    if (chdir(packagefile_dir) != 0) {
        printf("Error: Could not change to package directory: %s\n", packagefile_dir);
        free(packagefile_dir);
        return 1;
    }
    
    // Ejecutar Packagefile con Python3 silenciosamente
    snprintf(command, sizeof(command), "python3 Packagefile");
    result = system(command);
    
    // Volver al directorio original
    chdir(original_dir);
    free(packagefile_dir);
    
    if (result != 0) {
        printf("Warning: Packagefile execution returned non-zero exit code\n");
    }
    
    return 0;
}

int install_package(const char *package_name) {
    char wpk_file[MAX_BUFFER_SIZE];
    char extract_dir[MAX_BUFFER_SIZE];
    char temp_dir[MAX_BUFFER_SIZE];
    
    // Crear nombres de archivos temporales
    snprintf(wpk_file, sizeof(wpk_file), "/tmp/%s.wpk", package_name);
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/wpk_%s_XXXXXX", package_name);
    
    // Crear directorio temporal único
    if (!mkdtemp(temp_dir)) {
        printf("Error: Could not create temporary directory\n");
        return 1;
    }
    
    snprintf(extract_dir, sizeof(extract_dir), "%s/%s", temp_dir, package_name);
    
    // Descargar paquete
    if (download_package(package_name, wpk_file) != 0) {
        return 1;
    }
    
    // Extraer paquete
    if (extract_wpk(wpk_file, extract_dir) != 0) {
        remove(wpk_file);
        return 1;
    }
    
    // Ejecutar Packagefile si existe
    if (check_and_run_packagefile(extract_dir) != 0) {
        printf("Warning: Package configuration may have failed\n");
    }
    
    // Limpiar archivos temporales
    remove(wpk_file);
    
    // Limpiar directorio temporal
    char cleanup_cmd[MAX_BUFFER_SIZE];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf '%s'", temp_dir);
    system(cleanup_cmd);
    
    printf("==================\n");
    printf("Install done! Thank Jehovah!\n");
    
    return 0;
}

void show_usage(void) {
    printf("WPK - Water Package Manager\n");
    printf("Created with Jehova's blessing\n\n");
    printf("Usage:\n");
    printf("  wpk install <package>    Install a package\n");
    printf("  wpk list                 List available packages\n");
    printf("  wpk help                 Show this help\n\n");
    printf("Examples:\n");
    printf("  wpk install water\n");
    printf("  wpk install terminal\n");
    printf("  wpk list\n");
}

int main(int argc, char *argv[]) {
    // Verificar argumentos
    if (argc < 2) {
        show_usage();
        return 1;
    }
    
    // Inicializar curl globalmente
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Parsear comandos
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            printf("Error: Package name required\n");
            printf("Usage: wpk install <package>\n");
            curl_global_cleanup();
            return 1;
        }
        
        char package_name[MAX_PACKAGE_NAME];
        strncpy(package_name, argv[2], sizeof(package_name) - 1);
        package_name[sizeof(package_name) - 1] = '\0';
        
        // Convertir a mayúsculas para mostrar
        char package_display[MAX_PACKAGE_NAME];
        strcpy(package_display, package_name);
        for (int i = 0; package_display[i]; i++) {
            if (package_display[i] >= 'a' && package_display[i] <= 'z') {
                package_display[i] = package_display[i] - 'a' + 'A';
            }
        }
        
        int result = install_package(package_name);
        curl_global_cleanup();
        return result;
        
    } else if (strcmp(argv[1], "list") == 0) {
        int result = list_packages();
        curl_global_cleanup();
        return result;
        
    } else if (strcmp(argv[1], "help") == 0) {
        show_usage();
        curl_global_cleanup();
        return 0;
    } else {
        printf("Error: Unknown command '%s'\n", argv[1]);
        show_usage();
        curl_global_cleanup();
        return 1;
    }
}

