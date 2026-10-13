# Media Downloader GTK v1.1.0

Nueva versión de la aplicación de escritorio en español para descargar videos
y audio con yt-dlp y FFmpeg.

## Descargar e instalar

Instalador: **media-downloader-gtk_1.1.0-1_amd64.deb**.
Sistema probado: **Ubuntu 24.04, amd64 (Intel/AMD de 64 bits)**.

```sh
sudo apt install ./media-downloader-gtk_1.1.0-1_amd64.deb
```

El comando también actualiza una instalación anterior. Cierra y vuelve a abrir
la aplicación después de instalar. APT instala las dependencias necesarias.
Se adjunta `SHA256SUMS` para verificar el archivo descargado.

## Cambios

- Progreso basado en los bytes acumulados de video y audio. El total estimado
  queda fijo durante la descarga y se identifica con **≈**.
- Verificación del formato final con FFprobe antes de marcar una tarea como
  completada. Los archivos intermedios permanecen en una carpeta temporal.
- Reintentos automáticos configurables de **1 a 10**, con pausas progresivas
  para los fallos temporales de conexión, análisis y procesamiento.
- Conversión reintentada usando los archivos descargados cuando están disponibles.
- Numeración opcional de listas con prefijos como `001 -`, respetando la posición original.
- Confirmación al cerrar con tareas pendientes.
- Botón **Cancelar todas** para dos o más tareas pendientes, con confirmación.
- Selectores de formato, calidad, reintentos y preferencias sin marcas de check.

## Validación y límites

Pruebas de núcleo, memoria, descarga y conversión con medios generados localmente,
reintentos y confirmaciones de interfaz. La instalación del paquete se comprueba
en un contenedor Ubuntu 24.04 independiente.

El análisis de listas está limitado a los primeros 100 elementos. El historial
no se conserva entre sesiones. La disponibilidad del contenido depende del
sitio y de yt-dlp; los videos privados o inexistentes no se vuelven accesibles
mediante reintentos. El instalador no incluye una versión para Windows o macOS.

Código original bajo licencia MIT. Descarga solo contenido propio, de dominio
público o autorizado. La aplicación no evita DRM ni restricciones de acceso.
