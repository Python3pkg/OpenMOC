#include "Matrix.h"

/**
 * @brief Constructor initializes Matrix as a list of lists
 *        and sets the matrix dimensions.
 * @detail The matrix object uses a "lists of lists" structure (implemented as
 *         a map of lists) to allow for easy setting and incrementing of the 
 *         values in the object. When the matrix is needed to perform linear
 *         algebra operations, it is converted to compressed row storage (CSR)
 *         form. The matrix is ordered by cell (as opposed to by group) on the
 *         outside. Locks are used to make the matrix thread-safe against 
 *         concurrent writes the same value. One lock locks out multiple rows of
 *         the matrix at a time reprsenting multiple groups in the same cell.
 * @param num_x The number of cells in the x direction.
 * @param num_y The number of cells in the y direction.
 * @param num_groups The number of energy groups in each cell.
 */
Matrix::Matrix(int num_x, int num_y, int num_groups) {

  setNumX(num_x);
  setNumY(num_y);
  setNumGroups(num_groups);  
  _num_rows = _num_x*_num_y*_num_groups;
  
  /* Initialize variables */
  for (int i=0; i < _num_rows; i++) {
    std::map<int, FP_PRECISION> *values = new std::map<int, FP_PRECISION>;
    _LIL.push_back(*values);
  }

  _A = NULL;
  _IA = NULL;
  _JA = NULL;
  _DIAG = NULL;
  _modified = true;

  /* Allocate memory for OpenMP locks for each Matrix cell */ 
  _cell_locks = new omp_lock_t[_num_x*_num_y];

  /* Loop over all Matrix cells to initialize OpenMP locks */
  #pragma omp parallel for schedule(guided)
  for (int r=0; r < _num_x*_num_y; r++)
    omp_init_lock(&_cell_locks[r]);  
}


/**
 * @brief Destructor clears list of lists and deletes the arrays
 *        used to represent the matrix in CSR form.
 */
Matrix::~Matrix() {

  if (_A != NULL)
    delete [] _A;

  if (_IA != NULL)
    delete [] _IA;

  if (_JA != NULL)
    delete [] _JA;

  if (_DIAG != NULL)
    delete [] _DIAG;
  
  for (int i=0; i < _num_rows; i++)
    _LIL[i].clear();

  if (_cell_locks != NULL)
    delete [] _cell_locks;
}


/**
 * @brief Increment a value in the matrix.
 * @detail This method takes a cell and group of origin (cell/group from)
 *         and cell and group of destination (cell/group to) and floating
 *         point value. The origin and destination are used to compute the
 *         row and column in the matrix. If a value exists for the row/column,
 *         the value is incremented by val; otherwise, it is set to val.
 * @param cell_from The origin cell.
 * @param group_from The origin group.
 * @param cell_to The destination cell.
 * @param group_from The destination group.
 * @param val The value used to increment the row/column location.
 */
void Matrix::incrementValue(int cell_from, int group_from,
                            int cell_to, int group_to, FP_PRECISION val) {

  if (cell_from >= _num_x*_num_y || cell_from < 0)
    log_printf(ERROR, "Unable to increment Matrix value for cell_from %i"
               " which is not between 0 and %i", cell_from, _num_x*_num_y-1);
  else if (cell_to >= _num_x*_num_y || cell_to < 0)
    log_printf(ERROR, "Unable to increment Matrix value for cell_to %i"
               " which is not between 0 and %i", cell_from, _num_x*_num_y-1);
  else if (group_from >= _num_groups || group_from < 0)
    log_printf(ERROR, "Unable to increment Matrix value for group_from %i"
               " which is not between 0 and %i", group_from, _num_groups-1);
  else if (group_to >= _num_groups || group_to < 0)
    log_printf(ERROR, "Unable to increment Matrix value for group_to %i"
               " which is not between 0 and %i", group_to, _num_groups-1);
  
  /* Atomically increment the Matrix value from the
   * temporary array using mutual exclusion locks */
  omp_set_lock(&_cell_locks[cell_to]);

  int row = cell_to*_num_groups + group_to;
  int col = cell_from*_num_groups + group_from;
  _LIL[row][col] += val;
  
  /* Release Matrix cell mutual exclusion lock */
  omp_unset_lock(&_cell_locks[cell_to]);

  /* Set global modified flag to true */
  _modified = true;
}


/**
 * @brief Set a value in the matrix.
 * @detail This method takes a cell and group of origin (cell/group from)
 *         and cell and group of destination (cell/group to) and floating
 *         point value. The origin and destination are used to compute the
 *         row and column in the matrix. The location specified by the
 *         row/column is set to val.
 * @param cell_from The origin cell.
 * @param group_from The origin group.
 * @param cell_to The destination cell.
 * @param group_from The destination group.
 * @param val The value used to set the row/column location.
 */
void Matrix::setValue(int cell_from, int group_from,
                      int cell_to, int group_to, FP_PRECISION val) {

  if (cell_from >= _num_x*_num_y || cell_from < 0)
    log_printf(ERROR, "Unable to set Matrix value for cell_from %i"
               " which is not between 0 and %i", cell_from, _num_x*_num_y-1);
  else if (cell_to >= _num_x*_num_y || cell_to < 0)
    log_printf(ERROR, "Unable to set Matrix value for cell_to %i"
               " which is not between 0 and %i", cell_from, _num_x*_num_y-1);
  else if (group_from >= _num_groups || group_from < 0)
    log_printf(ERROR, "Unable to set Matrix value for group_from %i"
               " which is not between 0 and %i", group_from, _num_groups-1);
  else if (group_to >= _num_groups || group_to < 0)
    log_printf(ERROR, "Unable to set Matrix value for group_to %i"
               " which is not between 0 and %i", group_to, _num_groups-1);
  
  /* Atomically set the Matrix value from the
   * temporary array using mutual exclusion locks */
  omp_set_lock(&_cell_locks[cell_to]);

  int row = cell_to*_num_groups + group_to;
  int col = cell_from*_num_groups + group_from;
  _LIL[row][col] = val;
  
  /* Release Matrix cell mutual exclusion lock */
  omp_unset_lock(&_cell_locks[cell_to]);

  /* Set global modified flag to true */
  _modified = true;  
}


/**
 * @brief Clear all values in the matrix list of lists.
 */
void Matrix::clear() {
  for (int i=0; i < _num_rows; i++)
    _LIL[i].clear();

  _modified = true;
}


/**
 * @brief Convert the matrix lists of lists to compressed row (CSR) storage 
 *        form.
 */
void Matrix::convertToCSR() {
  
  /* Get number of nonzero values */
  int NNZ = getNNZ();
  
  /* Allocate memory for arrays */
  if (_A != NULL)
    delete [] _A;
  
  if (_IA != NULL)
    delete [] _IA;
  
  if (_JA != NULL)
    delete [] _JA;
  
  if (_DIAG != NULL)
    delete [] _DIAG;
  
  _A = new FP_PRECISION[NNZ];
  _IA = new int[_num_rows+1];
  _JA = new int[NNZ];
  _DIAG = new FP_PRECISION[_num_rows];
  std::fill_n(_DIAG, _num_rows, 0.0);  
  
  /* Form arrays */
  int j = 0;
  std::map<int, FP_PRECISION>::iterator iter;
  for (int row=0; row < _num_rows; row++) {
    _IA[row] = j;
    for (iter = _LIL[row].begin(); iter != _LIL[row].end(); ++iter) {
      if (iter->second != 0.0) {
        _JA[j] = iter->first;
        _A[j] = iter->second;
        
        if (row == iter->first)
          _DIAG[row] = iter->second;
        
        j++;
      }
    }
  }
  
  _IA[_num_rows] = NNZ;

  /* Reset flat indicating the CSR objects have the same values as the 
   * LIL object */
  _modified = false;
}



/**
 * @brief Print the matrix object to the log file.
 */
void Matrix::printString() {

  /* Convert to CSR form */
  convertToCSR();

  std::stringstream string;
  string << std::setprecision(6) << std::endl;
  string << " Matrix Object " << std::endl;
  string << " Num rows: " << _num_rows << std::endl;
  string << " NNZ     : " << getNNZ() << std::endl;

  for (int row=0; row < _num_rows; row++) {
    for (int i = _IA[row]; i < _IA[row+1]; i++)
      string << " ( " << row << ", " << _JA[i] << "): " << _A[i] << std::endl;
  }

  string << "End Matrix " << std::endl;
  
  log_printf(NORMAL, string.str().c_str());
}


/**
 * @brief Get a value in the matrix.
 * @detail This method takes a cell and group of origin (cell/group from)
 *         and cell and group of destination (cell/group to).
 *         The origin and destination are used to compute the
 *         row and column in the matrix. The value at the location specified 
 *         by the row/column is returned.
 * @param cell_from The origin cell.
 * @param group_from The origin group.
 * @param cell_to The destination cell.
 * @param group_from The destination group.
 * @return The value at the corresponding row/column location.
 */
FP_PRECISION Matrix::getValue(int cell_from, int group_from,
                              int cell_to, int group_to) {
  int row = cell_to*_num_groups + group_to;
  int col = cell_from*_num_groups + group_from;
  return _LIL[row][col];
}


/**
 * @brief Get the A component of the CSR form of the matrix object.
 * @return A pointer to the A component of the CSR form matrix object.
 */
FP_PRECISION* Matrix::getA() {

  if (_modified)
    convertToCSR();
  
  return _A;
}


/**
 * @brief Get the IA component of the CSR form of the matrix object.
 * @return A pointer to the IA component of the CSR form matrix object.
 */
int* Matrix::getIA() {

  if (_modified)
    convertToCSR();

  return _IA;
}


/**
 * @brief Get the JA component of the CSR form of the matrix object.
 * @return A pointer to the JA component of the CSR form matrix object.
 */
int* Matrix::getJA() {

  if (_modified)
    convertToCSR();
    
  return _JA;
}


/**
 * @brief Get the diagonal component of the matrix object.
 * @return A pointer to the diagonal component of the matrix object.
 */
FP_PRECISION* Matrix::getDiag() {

  if (_modified)
    convertToCSR();
    
  return _DIAG;
}


/**
 * @brief Get the number of cells in the x dimension.
 * @return The number of cells in the x dimension.
 */
int Matrix::getNumX() {
  return _num_x;
}


/**
 * @brief Get the number of cells in the x dimension.
 * @return The number of cells in the x dimension.
 */
int Matrix::getNumY() {
  return _num_y;
}


/**
 * @brief Get the number of groups in each cell.
 * @return The number of groups in each cell.
 */
int Matrix::getNumGroups() {
  return _num_groups;
}


/**
 * @brief Get the number of rows in the matrix.
 * @return The number of rows in the matrix.
 */
int Matrix::getNumRows() {
  return _num_rows;
}


/**
 * @brief Get the number of non-zero values in the matrix.
 * @return The number of non-zero values in the matrix.
 */
int Matrix::getNNZ() {

  int NNZ = 0;
  std::map<int, FP_PRECISION>::iterator iter;
  for (int row=0; row < _num_rows; row++) {
    for (iter = _LIL[row].begin(); iter != _LIL[row].end(); ++iter) {
      if (iter->second != 0.0)
        NNZ++;
    }
  }

  return NNZ;
}


/**
 * @brief Set the number of cells in the x dimension.
 * @param num_x The number of cells in the x dimension.
 */
void Matrix::setNumX(int num_x) {

  if (num_x < 1)
    log_printf(ERROR, "Unable to set Matrix num x to non-positive value %i",
               num_x);

  _num_x = num_x;
}


/**
 * @brief Set the number of cells in the y dimension.
 * @param num_y The number of cells in the y dimension.
 */
void Matrix::setNumY(int num_y) {

  if (num_y < 1)
    log_printf(ERROR, "Unable to set Matrix num y to non-positive value %i",
               num_y);

  _num_y = num_y;
}


/**
 * @brief Set the number of groups in each cell.
 * @param num_groups The number of groups in each cell.
 */
void Matrix::setNumGroups(int num_groups) {

  if (num_groups < 1)
    log_printf(ERROR, "Unable to set Matrix num groups to non-positive value"
               " %i", num_groups);

  _num_groups = num_groups;
}
