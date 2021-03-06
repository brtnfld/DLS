/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
* Copyright by The HDF Group. *
* Copyright by the Board of Trustees of the University of Illinois. *
* All rights reserved. *
* *
* This file is part of HDF5. The full HDF5 copyright notice, including *
* terms governing use, modification, and redistribution, is contained in *
* the files COPYING and Copyright.html. COPYING can be found at the root *
* of the source code distribution tree; Copyright.html can be found at the *
* root level of an installed copy of the electronic HDF5 document set and *
* is linked from the top-level documents page. It can also be found at *
* http://hdfgroup.org/HDF5/doc/Copyright.html. If you do not have *
* access to either file, you may request a copy from help@hdfgroup.org. *
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*
* This example how to work with extendible datasets. The dataset
* must be chunked in order to be extendible.
*
*/
#include <string.h>
#include "hdf5.h"
#include "stdlib.h"
#include <time.h>
#include <unistd.h>
#define FILENAME "extend.h5"
#define DATASETNAME "ExtendibleArray"
#define RANK 3
#define DIM1 512 //1024
#define DIM2 512 // 1024
#define NTIM 1024  // 4000
#define NTIM_IO 512
#define MULTI
int
main (int argc, char **argv)
{
  hid_t file; /* handles */
  hid_t dataspace, dataset;
  hid_t filespace, memspace;
  hid_t prop, dataxfer_plist_id;
  hsize_t dims[3] = {1, DIM1, DIM2}; /* dataset dimensions at creation time */
  hsize_t maxdims[3] = {H5S_UNLIMITED, DIM1, DIM2};
  herr_t status;
  hsize_t chunk_dims[3] = {1, DIM1, DIM2};
  /* Variables used in extending and writing to the extended portion of dataset */
  hsize_t size[3] = {0, 0, 0};
  hsize_t offset[3];
  hsize_t dimsext[3] = {1, DIM1, DIM2}; /* extend dimensions */
  int dataext[NTIM_IO][DIM1][DIM2];
  /* Variables used in reading data back */
  hsize_t chunk_dimsr[2];
  hsize_t dimsr[2];
  hsize_t i, j, k, t, icnt;
  int rdata[10][3];
  herr_t status_n;
  int rank, rank_chunk;
  hid_t	plist_id; /* property list identifier */
  double t1, t2, ttol;
  double mb;
  dataxfer_plist_id = H5P_DEFAULT;
  int n;
  int mpi_size=1, mpi_rank=0;
#ifdef MULTI
    H5D_rw_multi_t *multi_info;
#endif

#ifdef PARALLEL
  /*
   * MPI variables
   */
  MPI_Comm comm = MPI_COMM_WORLD;
  MPI_Info info = MPI_INFO_NULL;
  /*
   * Initialize MPI
   */
  MPI_Init(&argc, &argv);
  MPI_Comm_size(comm, &mpi_size);
  MPI_Comm_rank(comm, &mpi_rank);
  MPI_Info_create(&info);
  /* MPI_Info_set(info, "IBM_largeblock_io", "true"); */
  /* MPI_Info_set(info,"stripping_unit","4194304"); */
  /* MPI_Info_set(info,"cb_buffer_size","4194304", error); */
  char mpi_name[100];
  int mpi_name_len;
  MPI_Get_processor_name(mpi_name, &mpi_name_len);
  /*
   * Set up file access property list with parallel I/O access
   */
  plist_id = H5Pcreate(H5P_FILE_ACCESS);
  H5Pset_fapl_mpio(plist_id, comm, info);
  printf("We are fully MPI parallel... %s\n", mpi_name);
  dataxfer_plist_id = H5Pcreate(H5P_DATASET_XFER);
  H5Pset_dxpl_mpio(dataxfer_plist_id, H5FD_MPIO_INDEPENDENT);
  dimsext[1] = DIM1 * mpi_size;
  maxdims[1] = DIM1 * mpi_size;
#else
  plist_id = H5Pcreate(H5P_FILE_ACCESS);
#endif

#ifdef MULTI
    multi_info = (H5D_rw_multi_t *)malloc(NTIM_IO*sizeof(H5D_rw_multi_t));
#endif

  char *hostname = calloc(100, sizeof(char));
  gethostname(hostname, 100);
  /* Create the data space with unlimited dimensions. */
  dataspace = H5Screate_simple (RANK, dimsext, maxdims);
  status = H5Pset_alignment( plist_id, 64*1024, 4*1024*1024);
  H5Pset_fclose_degree(plist_id, H5F_CLOSE_STRONG);
  /* Create a new file. If file exists its contents will be overwritten. */
  file = H5Fcreate (strcat(hostname, FILENAME), H5F_ACC_TRUNC, H5P_DEFAULT, plist_id);
  H5Pclose(plist_id);
  /* Modify dataset creation properties, i.e. enable chunking */
  prop = H5Pcreate (H5P_DATASET_CREATE);
  status = H5Pset_chunk (prop, RANK, chunk_dims);
  /* Create a new dataset within the file using chunk
     creation properties. */
  hid_t dset_acc_plist = H5Pcreate(H5P_DATASET_ACCESS);
  H5Pset_chunk_cache(dset_acc_plist, 10, 10*DIM1*DIM2*sizeof(int), 1.0);
  dataset = H5Dcreate2 (file, DATASETNAME, H5T_NATIVE_INT, dataspace,
			H5P_DEFAULT, prop, dset_acc_plist);
  H5Pclose(dset_acc_plist);
  ttol = 0.;
  icnt = 0;
  srand(time(NULL));
  for (j = 0; j < NTIM_IO; j++) {
    for (i = 0; i < dims[1]; i++) {
      for (k = 0; k < dims[2]; k++) {
	dataext[j][i][k] = (int) j;
      }
    }
  }
  n = 0;
  for (t = 0; t < NTIM; t++) {
    if(t % NTIM_IO == 0 && mpi_rank == 0)
      printf("slice number %lld, time %f \n", t, ttol);
    /* Extend the dataset. */
    //    size[0] = t+1;
    if( t % NTIM_IO == 0 ) {
      size[0] += NTIM_IO;
      size[1] = dims[1]*mpi_size;
      size[2] = dims[2];
      status = H5Dset_extent (dataset, size);
    }
    /* Select a hyperslab in extended portion of dataset */
    filespace = H5Dget_space (dataset);
    offset[0] = t;
    offset[1] = 0;
#ifdef PARALLEL
    offset[1] = dims[1]*mpi_rank;
#endif
    offset[2] = 0;
    dimsext[0] = 1;
    status = H5Sselect_hyperslab (filespace, H5S_SELECT_SET, offset, NULL,
				  dims, NULL);
    /* Define memory space */
    memspace = H5Screate_simple (RANK, dims, NULL);
#ifdef PARALLEL
    t1 = MPI_Wtime();
#else
    clock_t t1 = clock();
#endif

    multi_info[n].dset_id = dataset;
    multi_info[n].mem_type_id = H5T_NATIVE_INT;
    multi_info[n].mem_space_id = memspace;
    multi_info[n].dset_space_id = filespace;
    multi_info[n].u.wbuf = &dataext[n][0][0];
    n += 1;
    if(n == NTIM_IO) {
      status = H5Dwrite_multi(file, dataxfer_plist_id, n, multi_info);
      n = 0;
    }

    /* Write the data to the extended portion of dataset */
    //status = H5Dwrite (dataset, H5T_NATIVE_INT, memspace, filespace,
      // dataxfer_plist_id, &dataext[n][0][0]);

      //n += 1;
      //status = H5Sclose (memspace);
    //    if (t == NTIM - 1) {
    //  printf("Flushing dataset...\n");
    //  H5Fflush(dataset, H5F_SCOPE_LOCAL);
    //}
#ifdef PARALLEL
    t2 = MPI_Wtime();
    ttol += (t2 - t1);
#else
    clock_t t2 = clock();
    ttol += (double)(t2 - t1)/ CLOCKS_PER_SEC;
#endif
    /* printf( "Elapsed time is %f\n", ttol ); */
    icnt += 1;
  }
  mb= (double)sizeof(int)*(double)(DIM1*DIM2)*icnt/1048576.;
  printf("%5.2f MB, %5.2f seconds, %5.2f MB/s\n", mb, ttol, mb/ttol);
  /* Close resources */
  status = H5Dclose (dataset);
  status = H5Sclose (filespace);
  status = H5Sclose (dataspace);
  status = H5Pclose (prop);
  status = H5Pclose (dataxfer_plist_id);
  status = H5Fclose (file);
#ifdef PARALLEL
  MPI_Finalize();
#endif
  return 0;
}
