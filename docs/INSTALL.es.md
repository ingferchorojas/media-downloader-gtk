# Instalar Media Downloader GTK 1.1.0

El instalador publicado es para **Ubuntu 24.04, arquitectura amd64 (Intel/AMD de 64 bits)**.
La aplicación tiene una interfaz en español. No necesitas compilar el código.

## Instalación

1. [Descarga el instalador .deb](https://github.com/ingferchorojas/media-downloader-gtk/releases/download/v1.1.0/media-downloader-gtk_1.1.0-1_amd64.deb).
2. Abre una terminal en la carpeta donde guardaste el instalador y ejecuta:

   ```sh
   sudo apt install ./media-downloader-gtk_1.1.0-1_amd64.deb
   ```

3. Introduce tu contraseña cuando el sistema la solicite. APT instalará las dependencias necesarias, como yt-dlp y FFmpeg.
4. Abre **Media Downloader** desde el menú de aplicaciones, o ejecuta `media-downloader-gtk`.

Este mismo comando actualiza una instalación anterior. Después de actualizar,
cierra y vuelve a abrir la aplicación. Ejecuta la aplicación con tu usuario habitual.

## Descargar una lista como MP3

1. Pega la URL y pulsa **Analizar**.
2. Elige **Audio · MP3** y la carpeta de destino.
3. Selecciona los elementos de la lista. El análisis muestra hasta los primeros 100.
4. Si quieres prefijos como `001 -`, activa **Numerar videos automáticamente**.
5. Elige entre 1 y 10 reintentos automáticos y pulsa **Descargar**.
6. Confirma que quieres añadir los elementos a la cola.

Cada elemento se marca como completado después de verificar su formato. Una
conversión fallida muestra un error y conserva los archivos temporales para
reintentarla. **Cancelar todas** aparece con dos o más tareas pendientes y pide
confirmación. Cerrar con tareas pendientes también pide confirmación.

## Actualizaciones y problemas

La aplicación ofrece comprobar actualizaciones de yt-dlp y avisa cuando hay una
nueva versión del programa. Los sitios cambian; mantén yt-dlp actualizado. Consulta
[los requisitos del motor para YouTube](https://github.com/yt-dlp/yt-dlp/wiki/EJS) si
el mensaje de error menciona componentes EJS o un motor de JavaScript.

Si persiste un problema, abre una [incidencia](https://github.com/ingferchorojas/media-downloader-gtk/issues)
con la versión del programa, tu sistema y el texto del error. No compartas
contraseñas ni datos privados.

Para desinstalar:

```sh
sudo apt remove media-downloader-gtk
```

Descarga únicamente contenido propio, de dominio público o que tengas autorización para descargar.
