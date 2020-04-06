/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:     vfd_swmr_reader.c
 *              (copied and modified from swmr_reader.c)
 *
 * Purpose:     Reads data from a randomly selected subset of the datasets
 *              in the VFD SWMR test file.
 *
 *              This program is intended to run concurrently with the
 *              vfd_swmr_writer program.
 *
 *-------------------------------------------------------------------------
 */

/***********/
/* Headers */
/***********/

#include "h5test.h"
#include "vfd_swmr_common.h"

/********************/
/* Local Prototypes */
/********************/

static int check_dataset(hid_t fid, hbool_t verbose, FILE *verbose_file,
    const char *sym_name, symbol_t *record, hid_t rec_sid);
static int read_records(const char *filename, hbool_t verbose, FILE *verbose_file,
    unsigned random_seed, unsigned long nseconds, unsigned poll_time,
    unsigned ncommon, unsigned nrandom);

/*******************/
/* Local Variables */
/*******************/

static hid_t symbol_tid = -1;   /* The type ID for the SWMR datasets */


/*-------------------------------------------------------------------------
 * Function:    check_dataset
 *
 * Purpose:     For a given dataset, checks to make sure that the stated
 *              and actual sizes are the same.  If they are not, then
 *              we have an inconsistent dataset due to a SWMR error.
 *
 * Parameters:  hid_t fid
 *              The SWMR test file's ID.
 *
 *              hbool_t verbose
 *              Whether verbose console output is desired.
 *
 *              FILE *verbose_file
 *              File handle for verbose output
 *
 *              const char *sym_name
 *              The name of the dataset from which to read.
 *
 *              symbol_t *record
 *              Memory for the record.  Must be pre-allocated.
 *
 *              hid_t rec_sid
 *              The memory dataspace for access.  It's always the same so
 *              there is no need to re-create it every time this function
 *              is called.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
check_dataset(hid_t fid, hbool_t verbose, FILE *verbose_file,
    const char *sym_name, symbol_t *record, hid_t rec_sid)
{
    int fill_count = 0;         /* # of times fill value (0) was read
                                 * instead of the expected value.
                                 */
    hid_t dsid;                 /* Dataset ID */
    hid_t file_sid;             /* Dataset's space ID */
    hssize_t snpoints;          /* Number of elements in dataset */
    hsize_t start[2] = {0, 0}, count[2] = {1, 1};   /* Hyperslab selection values */

    HDassert(fid >= 0);
    HDassert(sym_name);
    HDassert(record);
    HDassert(rec_sid >= 0);

    /* Open dataset for symbol */
    if((dsid = H5Dopen2(fid, sym_name, H5P_DEFAULT)) < 0) {
        fprintf(stderr, "%s.%d: H5Dopen2 failed\n", __func__, __LINE__);
        goto error;
    }

    /* Get the dataset's dataspace */
    if((file_sid = H5Dget_space(dsid)) < 0) {
        fprintf(stderr, "%s.%d: H5Dget_space failed\n", __func__, __LINE__);
        goto error;
    }

    /* Get the number of elements (= records, for 1-D datasets) */
    if((snpoints = H5Sget_simple_extent_npoints(file_sid)) < 0) {
        fprintf(stderr, "%s.%d: H5Sget_simple_extent_npoints failed\n",
            __func__, __LINE__);
        goto error;
    }

    /* Emit informational message */
    if(verbose)
        HDfprintf(verbose_file, "READER: Symbol = '%s', # of records = %lld\n", sym_name, (long long)snpoints);

    /* Check if there are records for symbol */
    if(snpoints > 0) {
        /* Choose the last record in the dataset */
        start[1] = (hsize_t)(snpoints - 1);
        if(H5Sselect_hyperslab(file_sid, H5S_SELECT_SET, start, NULL, count, NULL) < 0) {
            fprintf(stderr, "%s.%d: H5Sselect_hyperslab failed\n",
                __func__, __LINE__);
            goto error;
        }

        /* Read record from dataset */
        record->rec_id = UINT64_MAX;
        if(H5Dread(dsid, symbol_tid, rec_sid, file_sid, H5P_DEFAULT, record) < 0) {
            fprintf(stderr, "%s.%d: H5Dread failed\n", __func__, __LINE__);
            goto error;
        }

        /* Verify record value */
        if (record->rec_id != start[1] && record->rec_id == 0)
            fill_count++;
        else if (record->rec_id != start[1]) {
            struct timeval tv;

            HDgettimeofday(&tv, NULL);

            if(verbose) {
                HDfprintf(verbose_file, "*** READER ERROR ***\n");
                HDfprintf(verbose_file, "Incorrect record value!\n");
                HDfprintf(verbose_file,
                    "Time = %jd.%06jd, Symbol = '%s'"
                    ", # of records = %" PRIdHSIZE
                    ", record->rec_id = %" PRIu64 "\n",
                    (intmax_t)tv.tv_sec, (intmax_t)tv.tv_usec,
                    sym_name, snpoints, record->rec_id);
            } /* end if */
            fprintf(stderr,
                "%s.%d: record value %" PRIu64 " != %" PRIuHSIZE "\n",
                __func__, __LINE__, record->rec_id, start[1]);
            goto error;
        } /* end if */
    } /* end if */

    /* Close the dataset's dataspace */
    if(H5Sclose(file_sid) < 0) {
        fprintf(stderr, "%s.%d: H5Sclose failed\n", __func__, __LINE__);
        goto error;
    }

    /* Close dataset for symbol */
    if(H5Dclose(dsid) < 0) {
        fprintf(stderr, "%s.%d: H5Dclose failed\n", __func__, __LINE__);
        goto error;
    }

    return fill_count;

error:
    H5E_BEGIN_TRY {
        H5Sclose(file_sid);
        H5Dclose(dsid);
    } H5E_END_TRY;

    return -1;
} /* end check_dataset() */


/*-------------------------------------------------------------------------
 * Function:    read_records
 *
 * Purpose:     For a given dataset, checks to make sure that the stated
 *              and actual sizes are the same.  If they are not, then
 *              we have an inconsistent dataset due to a SWMR error.
 *
 *              The "common" datasets are a random selection from among
 *              the level 0 datasets.  The "random" datasets are a random
 *              selection from among all the file's datasets.  This scheme
 *              ensures that the level 0 datasets are interrogated vigorously.
 *
 * Parameters:  const char *filename
 *              The SWMR test file's name.
 *
 *              hbool_t verbose
 *              Whether verbose console output is desired.
 *
 *              FILE *verbose_file
 *              File handle for verbose output
 *
 *              unsigned random_seed
 *              Random seed for the file (used for verbose logging)
 *
 *              unsigned long nseconds
 *              The amount of time to read records (ns).
 *
 *              unsigned poll_time
 *              The amount of time to sleep (s).
 *
 *              unsigned ncommon
 *              The number of common/non-random datasets that will be opened.
 *
 *              unsigned nrandom
 *              The number of random datasets that will be opened.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
read_records(const char *filename, hbool_t verbose, FILE *verbose_file,
    unsigned random_seed, unsigned long nseconds, unsigned poll_time,
    unsigned ncommon, unsigned nrandom)
{
    time_t start_time;          /* Starting time */
    time_t curr_time;           /* Current time */
    symbol_info_t **sym_com = NULL;     /* Pointers to array of common dataset IDs */
    symbol_info_t **sym_rand = NULL;    /* Pointers to array of random dataset IDs */
    hid_t mem_sid;              /* Memory dataspace ID */
    hid_t fid;                  /* SWMR test file ID */
    hid_t fapl;                 /* file access property list */
    symbol_t record;            /* The record to read from the dataset */
    unsigned v;                 /* Local index variable */
    hbool_t use_log_vfd = FALSE;    /* Use the log VFD (set this manually) */
    H5F_vfd_swmr_config_t *config = NULL;   /* Configuration for VFD SWMR */

    HDassert(filename);
    HDassert(nseconds != 0);
    HDassert(poll_time != 0);

    /* Reset the record */
    /* (record's 'info' field might need to change for each record read, also) */
    HDmemset(&record, 0, sizeof(record));

    /* Emit informational message */
    if(verbose)
        HDfprintf(verbose_file, "READER: Choosing datasets\n");

    /* Allocate space for 'common' datasets, if any */
    if(ncommon > 0) {
        /* Allocate array to hold pointers to symbols for common datasets */
        if(NULL == (sym_com = (symbol_info_t **)HDmalloc(sizeof(symbol_info_t *) * ncommon))) {
            fprintf(stderr, "%s.%d: malloc failed\n", __func__, __LINE__);
            goto error;
        }

        /* Open the common datasets */
        for(v = 0; v < ncommon; v++) {
            unsigned offset;                /* Offset of symbol to use */

            /* Determine the offset of the symbol, within level 0 symbols */
            /* (level 0 symbols are the most common symbols) */
            offset = (unsigned)((unsigned)HDrandom() % symbol_count[0]);
            sym_com[v] = &symbol_info[0][offset];

            /* Emit informational message */
            if(verbose)
                HDfprintf(verbose_file, "READER: Common symbol #%u = '%s'\n", v, symbol_info[0][offset].name);
        } /* end for */
    } /* end if */

    /* Allocate space for 'random' datasets, if any */
    if(nrandom > 0) {
        /* Allocate array to hold pointers to symbols for random datasets */
        if(NULL == (sym_rand = (symbol_info_t **)HDmalloc(sizeof(symbol_info_t *) * nrandom))) {
            fprintf(stderr, "%s.%d: malloc failed\n", __func__, __LINE__);
            goto error;
        }

        /* Determine the random datasets */
        for(v = 0; v < nrandom; v++) {
            symbol_info_t *sym;         /* Symbol to use */

            /* Determine the symbol, within all symbols */
            if(NULL == (sym = choose_dataset(NULL, NULL)))
                return -1;
            sym_rand[v] = sym;

            /* Emit informational message */
            if(verbose)
                HDfprintf(verbose_file, "READER: Random symbol #%u = '%s'\n", v, sym->name);
        } /* end for */
    } /* end if */

    /* Create a dataspace for the record to read */
    if((mem_sid = H5Screate(H5S_SCALAR)) < 0) {
        fprintf(stderr, "%s.%d: H5Screate failed\n", __func__, __LINE__);
        goto error;
    }

    /* Emit informational message */
    if(verbose)
        HDfprintf(verbose_file, "READER: Going to open file and read records\n");

    /* Get the starting time */
    start_time = HDtime(NULL);
    curr_time = start_time;

    /* Create file access property list */
    if((fapl = h5_fileaccess()) < 0) {
        fprintf(stderr, "%s.%d: h5_fileaccess failed\n", __func__, __LINE__);
        goto error;
    }

    /* Log I/O when verbose output it enbabled */
    if(use_log_vfd) {
        char verbose_name[1024];

        HDsnprintf(verbose_name, sizeof(verbose_name), "vfd_swmr_reader.log.%u", random_seed);

        H5Pset_fapl_log(fapl, verbose_name, H5FD_LOG_ALL, (size_t)(512 * 1024 * 1024));
    } /* end if */

    /*
     * Set up to open the file with VFD SWMR configured.
     */
    /* Enable page buffering */
    if(H5Pset_page_buffer_size(fapl, 4096, 0, 0) < 0) {
        fprintf(stderr, "%s.%d: H5Pset_page_buffer_size failed\n",
            __func__, __LINE__);
        goto error;
    }

    /* Allocate memory for the configuration structure */
    if((config = (H5F_vfd_swmr_config_t *)HDcalloc(1, sizeof(H5F_vfd_swmr_config_t))) == NULL) {
        fprintf(stderr, "%s.%d: malloc failed\n", __func__, __LINE__);
        goto error;
    }

    config->version = H5F__CURR_VFD_SWMR_CONFIG_VERSION;
    config->tick_len = 4;
    config->max_lag = 5;
    config->writer = FALSE;
    config->md_pages_reserved = 128;
    HDstrcpy(config->md_file_path, "./my_md_file");

    /* Enable VFD SWMR configuration */
    if(H5Pset_vfd_swmr_config(fapl, config) < 0) {
        fprintf(stderr, "%s.%d: H5Pset_vfd_swmr_config failed\n",
            __func__, __LINE__);
        goto error;
    }

    /* Loop over reading records until [at least] the correct # of seconds have passed */
    while(curr_time < (time_t)(start_time + (time_t)nseconds)) {

        /* Emit informational message */
        if(verbose)
            HDfprintf(verbose_file, "READER: Opening file: %s\n", filename);

        /* Open the file with VFD SWMR configured */
        /* Remove H5E_BEGIN_TRY/END_TRY if you want to see the error stack */
        H5E_BEGIN_TRY {
            fid = H5Fopen(filename, H5F_ACC_RDONLY, fapl);
        } H5E_END_TRY;
        if(fid < 0) {
            HDfprintf(stderr, "READER: Error in opening the file: %s\n", filename);
            goto error;
        }

        /* Check 'common' datasets, if any */
        if(ncommon > 0) {
            /* Emit informational message */
            if(verbose)
                HDfprintf(verbose_file, "READER: Checking common symbols after FILE OPEN\n");

            /* Iterate over common datasets */
            for(v = 0; v < ncommon; v++) {
                /* Check common dataset */
                const int fill_count =
                    check_dataset(fid, verbose, verbose_file, sym_com[v]->name,
                        &record, mem_sid);
                if(fill_count < 0) {
                    fprintf(stderr, "%s.%d: check_dataset failed\n",
                        __func__, __LINE__);
                    goto error;
                }
                HDmemset(&record, 0, sizeof(record));
                if (fill_count > 0) {
                    fprintf(stderr, "common dataset: read fill at %d records\n",
                        fill_count);
                }
            } /* end for */
        } /* end if */

        /* Check 'random' datasets, if any */
        if(nrandom > 0) {
            /* Emit informational message */
            if(verbose)
                HDfprintf(verbose_file, "READER: Checking random symbols after FILE OPEN\n");

            /* Iterate over random datasets */
            for(v = 0; v < nrandom; v++) {
                /* Check random dataset */
                const int fill_count = check_dataset(fid, verbose, verbose_file,
                    sym_rand[v]->name, &record, mem_sid);
                if(fill_count < 0) {
                    fprintf(stderr, "%s.%d: check_dataset failed\n",
                        __func__, __LINE__);
                    goto error;
                }
                HDmemset(&record, 0, sizeof(record));
                if (fill_count > 0) {
                    fprintf(stderr, "random dataset: read fill at %d records\n",
                        fill_count);
                }
            } /* end for */
        } /* end if */

        /* Emit informational message */
        if(verbose)
            HDfprintf(verbose_file, "READER: Closing file\n");
            
        /* Close the file */
        if(H5Fclose(fid) < 0) {
            fprintf(stderr, "%s.%d: H5Fclose failed\n", __func__, __LINE__);
            goto error;
        }

        /* Sleep for the appropriate # of seconds */
        HDsleep(poll_time);

        /* Retrieve the current time */
        curr_time = HDtime(NULL);
    } /* end while */

    /* Close the memory dataspace */
    if(H5Sclose(mem_sid) < 0) {
        fprintf(stderr, "%s.%d: H5Sclose failed\n", __func__, __LINE__);
        goto error;
    }

    /* Close the fapl */
    if(H5Pclose(fapl) < 0) {
        fprintf(stderr, "%s.%d: H5Pclose failed\n", __func__, __LINE__);
        goto error;
    }

    if(config)
        HDfree(config);

    /* Emit informational message */
    if(verbose)
        HDfprintf(verbose_file, "READER: Closing datasets\n");

    /* Close 'random' datasets, if any */
    if(nrandom > 0) {
        /* Release array holding dataset ID's for random datasets */
        HDfree(sym_rand);
    } /* end if */

    /* Close 'common' datasets, if any */
    if(ncommon > 0) {
        /* Release array holding dataset ID's for common datasets */
        HDfree(sym_com);
    } /* end if */

    return 0;

error:
    if(config)
        HDfree(config);

    if(sym_rand)
        HDfree(sym_rand);

    if(sym_com)
        HDfree(sym_com);

    H5E_BEGIN_TRY {
        H5Sclose(mem_sid);
        H5Fclose(fid);
        H5Pclose(fapl);
    } H5E_END_TRY;

    return -1;
} /* end read_records() */

static void
usage(void)
{
    printf("\n");
    printf("Usage error!\n");
    printf("\n");
    printf("Usage: vfd_swmr_reader [-q] [-s <# of seconds to sleep between polling>]\n");
    printf("    [-h <# of common symbols to poll>] [-l <# of random symbols to poll>]\n");
    printf("    [-r <random seed>] <# of seconds to test>\n");
    printf("\n");
    printf("<# of seconds to test> must be specified.\n");
    printf("\n");
    printf("Defaults to verbose (no '-q' given), 1 second between polling ('-s 1'),\n");
    printf("5 common symbols to poll ('-h 5'), 10 random symbols to poll ('-l 10'),\n");
    printf("and will generate a random seed (no -r given).\n");
    printf("\n");
    HDexit(1);
}

int main(int argc, const char *argv[])
{
    long nseconds = 0;      /* # of seconds to test */
    int poll_time = 1;      /* # of seconds between polling */
    int ncommon = 5;        /* # of common symbols to poll */
    int nrandom = 10;       /* # of random symbols to poll */
    hbool_t verbose = TRUE; /* Whether to emit some informational messages */
    FILE *verbose_file = NULL;  /* File handle for verbose output */
    hbool_t use_seed = FALSE; /* Set to 1 if a seed was set on the command line */
    unsigned random_seed = 0;   /* Random # seed */
    unsigned u;             /* Local index variables */
    int temp;

    /* Parse command line options */
    if(argc < 2)
        usage();
    if(argc > 1) {
        u = 1;
        while(u < (unsigned)argc) {
            if(argv[u][0] == '-') {
                switch(argv[u][1]) {
                    /* # of common symbols to poll */
                    case 'h':
                        ncommon = HDatoi(argv[u + 1]);
                        if(ncommon < 0)
                            usage();
                        u += 2;
                        break;

                    /* # of random symbols to poll */
                    case 'l':
                        nrandom = HDatoi(argv[u + 1]);
                        if(nrandom < 0)
                            usage();
                        u += 2;
                        break;

                    /* Be quiet */
                    case 'q':
                        verbose = FALSE;
                        u++;
                        break;

                    /* Random # seed */
                    case 'r':
                        use_seed = TRUE;
                        temp = HDatoi(argv[u + 1]);
                        if(temp < 0)
                            usage();
                        else
                            random_seed = (unsigned)temp;
                        u += 2;
                        break;

                    /* # of seconds between polling */
                    case 's':
                        poll_time = HDatoi(argv[u + 1]);
                        if(poll_time < 0)
                            usage();
                        u += 2;
                        break;

                    default:
                        usage();
                        break;
                } /* end switch */
            } /* end if */
            else {
                /* Get the number of records to append */
                nseconds = HDatol(argv[u]);
                if(nseconds <= 0)
                    usage();

                u++;
            } /* end else */
        } /* end while */
    } /* end if */
    if(nseconds <= 0)
        usage();
    if(poll_time >= nseconds)
        usage();

    /* Set the random seed */
    if(!use_seed) {
        struct timeval t;

        HDgettimeofday(&t, NULL);
        random_seed = (unsigned)(t.tv_usec);
    } /* end if */
    HDsrandom(random_seed);

    /* Open output file */
    if(verbose) {
        char verbose_name[1024];

        HDsnprintf(verbose_name, sizeof(verbose_name), "vfd_swmr_reader.out.%u", random_seed);
        if(NULL == (verbose_file = HDfopen(verbose_name, "w"))) {
            HDfprintf(stderr, "READER: Can't open verbose output file!\n");
            HDexit(1);
        }
    } /* end if */

    /* Emit informational message */
    if(verbose) {
        HDfprintf(verbose_file, "READER: Parameters:\n");
        HDfprintf(verbose_file, "\t# of seconds between polling = %d\n", poll_time);
        HDfprintf(verbose_file, "\t# of common symbols to poll = %d\n", ncommon);
        HDfprintf(verbose_file, "\t# of random symbols to poll = %d\n", nrandom);
        HDfprintf(verbose_file, "\t# of seconds to test = %ld\n", nseconds);
    } /* end if */

    /* ALWAYS emit the random seed for possible debugging */
    HDfprintf(stdout, "READER: Using reader random seed: %u\n", random_seed);

    /* Emit informational message */
    if(verbose)
        HDfprintf(verbose_file, "READER: Generating symbol names\n");

    /* Generate dataset names */
    if(generate_symbols() < 0) {
        HDfprintf(stderr, "READER: Error generating symbol names!\n");
        HDexit(1);
    } /* end if */

    /* Create datatype for creating datasets */
    if((symbol_tid = create_symbol_datatype()) < 0) {
        HDfprintf(stderr, "READER: Error creating symbol datatype!\n");
        HDexit(1);
    }

    /* Reading records from datasets */
    if(read_records(FILENAME, verbose, verbose_file, random_seed, (unsigned long)nseconds, (unsigned)poll_time, (unsigned)ncommon, (unsigned)nrandom) < 0) {
        HDfprintf(stderr, "READER: Error reading records from datasets (random_seed = %u)!\n", random_seed);
        HDexit(1);
    } /* end if */

    /* Emit informational message */
    if(verbose)
        HDfprintf(verbose_file, "READER: Releasing symbols\n");

    /* Clean up the symbols */
    if(shutdown_symbols() < 0) {
        HDfprintf(stderr, "READER: Error releasing symbols!\n");
        HDexit(1);
    } /* end if */

    /* Emit informational message */
    if(verbose)
        HDfprintf(verbose_file, "READER: Closing objects\n");

    /* Close objects created */
    if(H5Tclose(symbol_tid) < 0) {
        HDfprintf(stderr, "READER: Error closing symbol datatype!\n");
        HDexit(1);
    } /* end if */

    return 0;
}
