/* Reads the world mesh and the lightmap of a level through the C interface.
 * usage: example_c_api file.bsp|file.map */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wrapper.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: %s file.bsp|file.map\n", argv[0]);
    return 1;
  }

  /* --8<-- [start:load] */
  if (QLib_GetApiVersion() != QLIB_API_VERSION) {
    printf("header and library do not match\n");
    return 1;
  }

  QLibLevel *level = NULL;
  size_t len = strlen(argv[1]);
  int isMap = len > 4 && (strcmp(argv[1] + len - 4, ".map") == 0 || strcmp(argv[1] + len - 4, ".MAP") == 0);
  QLibResult r = isMap ? QLibMap_Load(argv[1], NULL, &level) : QLibBsp_Load(argv[1], NULL, &level);
  if (r != QLIB_OK) {
    printf("%s\n", QLib_GetLastError());
    return 1;
  }
  if (isMap)
    QLibMap_GenerateGeometry(level); /* texture sizes and face types go before this */
  /* --8<-- [end:load] */

  /* --8<-- [start:mesh] */
  QLibSolidEntityInfo info;
  QLibLevel_GetSolidEntityInfo(level, 0, &info); /* entity 0 is the worldspawn */

  QLibVertex *vertices = malloc(sizeof(QLibVertex) * info.vertexCount);
  uint32_t *indices = malloc(sizeof(uint32_t) * info.indexCount);
  QLibSubmesh *submeshes = malloc(sizeof(QLibSubmesh) * info.submeshCount);
  QLibLevel_CopySolidEntityMesh(level, 0, vertices, info.vertexCount, indices, info.indexCount, submeshes,
                                info.submeshCount);
  printf("%s: %u vertices, %u submeshes\n", info.className, info.vertexCount, info.submeshCount);
  /* --8<-- [end:mesh] */

  /* --8<-- [start:lightmap] */
  QLibLightmapInfo lm;
  if (QLibLevel_GetLightmapInfo(level, &lm) == QLIB_OK) {
    uint32_t bytes = lm.width * lm.height * 4;
    uint8_t *rgba = malloc(bytes);
    QLibLevel_CopyLightmap(level, rgba, bytes);

    uint32_t changed = 0;
    QLibLevel_UpdateLightStyles(level, 1.5, &changed);
    if (changed)
      QLibLevel_CopyLightmap(level, rgba, bytes);
    printf("lightmap %ux%u, %u layers\n", lm.width, lm.height, lm.layerCount);
    free(rgba);
  }
  /* --8<-- [end:lightmap] */

  free(vertices);
  free(indices);
  free(submeshes);
  QLibLevel_Destroy(level);
  return 0;
}
