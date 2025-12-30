#include <math.h>
#include "float_types.h"

/* To allow compatability with original referenced arm matrix operations */
typedef struct
{
  uint16_t numRows;     /**< number of rows of the matrix.     */
  uint16_t numCols;     /**< number of columns of the matrix.  */
  float32_t *pData;     /**< points to the data of the matrix. */
} matrix_instance_f32;