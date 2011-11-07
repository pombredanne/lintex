/*------------------------------------------------------*
 | Author: Maurizio Loreti, aka MLO or (HAM) I3NOO      |
 | Work:   University of Padova - Department of Physics |
 |         Via F. Marzolo, 8 - 35131 PADOVA - Italy     |
 | Phone:  ++39(49) 827-7216     FAX: ++39(49) 827-7102 |
 | EMail:  loreti@padova.infn.it                        |
 | WWW:    http://wwwcdf.pd.infn.it/~loreti/mlo.html    |
 *------------------------------------------------------*

 Copyright (C) 2010-2011 Ryan Kavanagh <ryanakca@kubuntu.org>

 The lintex website now resides at:
   http://github.com/ryanakca/lintex


  Description: the command "lintex [-i] [-r] [dir1 [dir2 ...]]" scans
    the directories given as parameters (the default is the current
    directory), looking for TeX-related files no more needed and to be
    removed.  With the option -i the user is asked before actually
    removing any file; with the option -r, the given directories are
    scanned recursively.

  Environment: the program has been developed under Solaris 2; but
    should run on every system supporting opendir/readdir/closedir and
    stat.  The file names in the struct dirent (defined in <dirent.h>)
    are assumed to be null terminated (this is guaranteed under Solaris
    2).

  History:
    1.00 - 1996-07-05 , first release
    1.01 - 1996-07-25 , improved (modify time in nodes)
    1.02 - 1996-10-16 , list garbage files w/out .tex
    1.03 - 1997-05-21 , call to basename; uploaded to CTAN, where lives
                        in /tex-archive/support/lintex .  Added a man
                        page and a Makefile.
    1.04 - 1998-06-22 , multiple directories in the command line; -r
                        command option; -I and -R accepted, in addition
                        to -i and -r; more extensions in protoTree; code
                        cleanup.
    1.05 - 2001-12-02 , linked list structure optimized.
    1.06 - 2002-09-25 , added .pdf extension.
    1.07 - 2010-08-11 , don't delete read only files; delete .bbl BibTeX
                        files; add -k to keep final product; added
                        extensions for files generated by Beamer class
    1.08 - 2010-10-01 , Add support for different verbosity levels, be
                        relatively quiet by default; dropped support for
                        the DEBUG and FULLDEBUG compiler flags. It's all
                        taken care of with command line options now;
                        added a -p (pretend) flag that shows what we would
                        have removed, but doesn't do anything.
    1.09 - 2010-11-30 , Add support for removing files older than their
                        source; remove duplicate entry in usage; update
                        usage to not be wider than 72 characters.
    1.10 - 2011-01-30 , Also remove .thm (generated by ntheorem), .out
                        (generated by hyperref), .toc.old (memoir?); list
                        removal extensions in manpage.
    1.11 - 2011-11-07 , Also remove .synctex.gz files.

  TODO: It would be nice to have a config file where users can specify
    extensions they'd like to have removed. Default options could also
    be included.

  ---------------------------------------------------------------------*/

/**
 | Included files
**/

#include <stdio.h>              /* Standard library */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

#include <sys/types.h>          /* Unix proper */
#include <sys/stat.h>
#include <dirent.h>

/**
 | Definitions:
 | - LONG_ENOUGH: length of the buffer used to read the answer from the
 |   user, when the -i command option is specified;
 | - MAX_B_EXT: maximum length of the extension for backup files (including
 |   the leading dot and the trailing '\0').
 | - TRUE, FALSE: guess what?
 | - VERSION: lintex version
 | - QUIET, WHISPER, VERBOSE and DEBUG:
 |    * QUIET: no output
 |    * WHISPER: only print actions that are taken (ie. list files that are
 |      removed, default starting as of 1.08)
 |    * VERBOSE: list files on which actions are taken as well as those which
 |      aren't (default up to / including 1.07)
 |    * DEDUG: print debug information (originally FULLDEBUG compiler flag)
 |      means print everything you can for those who want to debug.
 |   Errors will be sent to stderr regardless of the output level.
**/

#define LONG_ENOUGH 48
#define MAX_B_EXT    8
#define TRUE         1
#define FALSE        0
#define VERSION    "1.11 (2011-11-07)"
#define QUIET        0
#define WHISPER      1
#define VERBOSE      2
#define DEBUG        3

/**
 | Type definitions:
 | - Froot: the root of a linked list structure, where file names having a
 |     given extension (pointed to by Froot.extension) will be stored;
 |     these linked lists are also used to store directory names (with fake
 |     extension strings).
 | - Fnode: an entry in the linked list of the file names; contains the
 |     file modification time, the file name and a pointer to the next node.
 |     As a side note, the so called 'struct hack', here used to store the
 |     file name, is not guaranteed to work by the current C ANSI standard;
 |     but no environment/compiler where it does not work is currently
 |     known.
**/

typedef struct sFroot {
  char *extension;
  struct sFnode *firstNode;
  struct sFnode *lastNode;
} Froot;

typedef struct sFnode {
  time_t mTime;
  struct sFnode *next;
  int write;
  char name[1];
} Fnode;

/**
 | Global variables:
 | - confirm: will be 0 or 1 according to the -i command option;
 | - recurse: will be 0 or 1 according to the -r command option;
 | - keep: will be 0 or 1 according to the -k command option;
 | - output_level: See the definitions above for more details;
 | - pretend: will be 0 or 1 according to -p command option;
 | - older: will be 0 or 1 according to -o command option;
 | - bExt: the extension for backup files: defaults to "~" (the emacs
 |   convention);
 | - n_bExt: the length of the previous string;
 | - programName: the name of the executable;
 | - protoTree: Froot's of the file names having extensions relevant to
 |   TeX.  ".tex" extensions are assumed to be pointed to by protoTree[0].
 | - keepTree: Froot's of the file names having extensions relevant to final
 |   generated documents.
**/

static int     confirm         = FALSE;
static int     recurse         = FALSE;
static int     keep            = FALSE;
static int     output_level    = WHISPER;
static int     pretend         = FALSE;
static int     older           = FALSE;
static char    bExt[MAX_B_EXT] = "~";
static size_t  n_bExt;
static char   *programName;

static Froot protoTree[] = {
  {".tex", 0, 0},                        /* Must be first */
  {".aux", 0, 0},
  {".bbl", 0, 0},
  {".blg", 0, 0},
  {".dvi", 0, 0},
  {".idx", 0, 0},
  {".ilg", 0, 0},
  {".ind", 0, 0},
  {".lof", 0, 0},
  {".log", 0, 0},
  {".lot", 0, 0},
  {".nav", 0, 0},
  {".out", 0, 0},
  {".out", 0, 0},
  {".pdf", 0, 0},
  {".ps",  0, 0},
  {".snm", 0, 0},
  {".thm", 0, 0},
  {".toc", 0, 0},
  {".toc.old", 0, 0},
  {".synctex.gz", 0, 0},
  {0, 0, 0}                              /* Must be last (sentinel) */
};

static Froot keepTree[] = {
  {".pdf", 0, 0},
  {".ps",  0, 0},
  {".dvi", 0, 0},
  {0, 0, 0}
};

/**
 | Procedure prototypes (in alphabetical order)
**/

static char  *baseName(char *);
static Froot *buildTree(char *, Froot *);
static void   clean(char *);
static void   examineTree(Froot *, char *);
static void   insertNode(char *, size_t, time_t, int, Froot *);
static void   noMemory(void);
static void   nuke(char *);
static void   putsMessage(char *, int);
static void   printTree(Froot *);
static void   releaseTree(Froot *);
static void   syntax(void);

/*---------------------------*
 | And now, our main program |
 *---------------------------*/

int main(
  int argc,
  char *argv[]
){
  Froot *dirNames;              /* To hold the directories to be scanned */
  Fnode *pFN;                   /* Running pointer over directory names  */
  int    to_bExt  = FALSE;      /* Flag "next parameter to bExt"         */

  /**
   | Scans the arguments appropriately; the required directories are stored
   | in the linked list starting at "dirNames".
  **/

  programName = baseName(argv[0]);

  if ((dirNames = calloc(2, sizeof(Froot))) == 0) {
    noMemory();
  }
  dirNames->extension = "argv";

  while (--argc) {
    if ((*++argv)[0] == '-') {
      switch ( (*argv)[1] ) {
        case 'i':   case 'I':
          confirm = TRUE;
          break;

        case 'r':   case 'R':
          recurse = TRUE;
          break;

        case 'k':   case 'K':
          keep = TRUE;
          break;

        case 'b':   case 'B':
          to_bExt = TRUE;
          break;

        case 'q':   case 'Q':
          output_level = QUIET;
          break;

        case 'v':   case 'V':
          output_level = VERBOSE;
          break;

        case 'd':   case 'D':
          output_level = DEBUG;
          break;

        case 'p':   case 'P':
          pretend = TRUE;
          break;

        case 'o':   case 'O':
          older = TRUE;
          break;

        default:
          syntax();
      }

    } else {
      if (to_bExt) {
        strcpy(bExt, *argv);
        to_bExt = FALSE;
      } else {
        insertNode(*argv, 0, 0, 0, dirNames);
      }
    }
  }

  if (to_bExt) {
    syntax();
  }
  n_bExt = strlen(bExt);

  /**
   | If no parameter has been given, clean the current directory
  **/

  if ((pFN = dirNames->firstNode) == 0) {
    clean(".");
  } else {
    while (pFN != 0) {
      clean(pFN->name);
      pFN = pFN->next;
    }
  }
  releaseTree(dirNames);

  return EXIT_SUCCESS;
}

/*------------------------------------------*
 | The called procedures (in logical order) |
 *------------------------------------------*/

static void insertNode(
  char   *name,
  size_t  lName,
  time_t  mTime,
  int write,
  Froot  *root
){

  /**
   | Creates a new Fnode, to be inserted at the _end_ of the linked
   | list pointed to by root->firstNode (i.e., the list is organized
   | as a "queue", a.k.a. "FIFO" list): if a new node cannot be created,
   | an error message is printed and the program aborted.
   | If "lName" is bigger than zero, the file name is represented by the
   | first lName characters of "name"; otherwise by the whole string in
   | "name".
  **/

  Fnode  *pFN;                  /* The new node created by insertNode */
  size_t  sSize;                /* Structure size                     */

  sSize = sizeof(Fnode) + (lName == 0 ? strlen(name) : lName);

  if ((pFN = malloc(sSize)) == 0) {
    noMemory();
  }
  pFN->mTime = mTime;
  pFN->write = write;
  pFN->next  = 0;

  if (lName == 0) {
    strcpy(pFN->name, name);
  } else {
    strncpy(pFN->name, name, lName);
    pFN->name[lName] = '\0';
  }

  if (root->lastNode == 0) {
    root->firstNode = pFN;
  } else {
    root->lastNode->next = pFN;
  }
  root->lastNode = pFN;
}

static void noMemory(void)
{
  fprintf(stderr, "%s: couldn't obtain heap memory\n", programName);
  exit(EXIT_FAILURE);
}

static void clean(
  char *dirName
){

  /**
   | Does the job for the directory "dirName".
   |
   | Builds a structure holding the TeX-related files, and does the
   | required cleanup; finally, removes the file structure.
   | If the list appended to "dirs" has been filled, recurse over the
   | tree of subdirectories.
  **/

  Froot *teXTree;               /* Root node of the TeX-related files  */
  Froot *dirs;                  /* Subdirectories in this directory    */
  Fnode *pFN;                   /* Running pointer over subdirectories */

  if ((dirs = calloc(2, sizeof(Froot))) == 0) {
    noMemory();
  }
  dirs->extension = "subs";

  if ((teXTree = buildTree(dirName, dirs)) != 0) {

    if (output_level >= DEBUG) {
      printTree(teXTree);
    }

    examineTree(teXTree, dirName);
    releaseTree(teXTree);
  }

  for (pFN = dirs->firstNode;   pFN != 0;   pFN = pFN->next) {
    clean(pFN->name);
  }
  releaseTree(dirs);
}

static Froot *buildTree(
  char  *dirName,
  Froot *subDirs
){

  /**
   | - Opens the required directory;
   | - allocates a structure to hold the names of the TeX-related files,
   |   initialized from the global structure "protoTree";
   | - starts a loop over all the files of the given directory.
  **/

  DIR           *pDir;         /* Pointer returned from opendir()    */
  struct dirent *pDe;          /* Pointer returned from readdir()    */
  Froot         *teXTree;      /* Root node of the TeX-related files */

  if (output_level >= DEBUG) {
    printf("* Scanning directory \"%s\" - confirm = %c, recurse = %c, ",
         dirName, (confirm ? 'Y' : 'N'), (recurse ? 'Y' : 'N')),
    printf("keep = %c\n", (keep ? 'Y' : 'N'));
    printf("* Editor trailer: \"%s\"\n", bExt);
    puts("------------------------------Phase 1: directory scan");
  }

  if ((pDir = opendir(dirName)) == 0) {
    fprintf(stderr,
            "%s: \"%s\" cannot be opened (or is not a directory)\n",
            programName, dirName);
    return 0;
  }

  if ((teXTree = malloc(sizeof(protoTree))) == 0) {
    noMemory();
  }
  memcpy(teXTree, protoTree, sizeof(protoTree));

  while ((pDe = readdir(pDir)) != 0) {
    char    tName[FILENAME_MAX];         /* Fully qualified file name       */
    struct  stat sStat;                  /* To be filled by stat(2)         */
    size_t  len;                         /* Lenght of the current file name */
    size_t  last;                        /* Index of its last character     */
    char   *pFe;                         /* Pointer to file extension       */

    /**
     | - Tests for empty inodes (already removed files);
     | - skips the . and .. (current and previous directory);
     | - tests the trailing part of the file name against the extension of
     |   the backup files, to be always deleted.
    **/

    if (pDe->d_ino == 0)                continue;
    if (strcmp(pDe->d_name, ".")  == 0) continue;
    if (strcmp(pDe->d_name, "..") == 0) continue;

    sprintf(tName, "%s/%s", dirName, pDe->d_name);

    len  = strlen(pDe->d_name);
    last = len - 1;

    if (n_bExt != 0) {                  /* If 0, no backup files to delete */
      int crit;                         /* What exceeds backup extensions  */

      crit = len - n_bExt;
      if (crit > 0   &&   strcmp(pDe->d_name + crit, bExt) == 0) {
        nuke(tName);
        continue;
      }
    }

    /**
     | If the file is a directory and the -r option has been given, stores
     | the directory name in the linked list pointed to by "subDirs", for
     | recursive calls.
     |
     | N.B.: if stat(2) fails, the file is skipped.
    **/

    if (stat(tName, &sStat) != 0) {
      fprintf(stderr, "File \"%s", tName);
      perror("\"");
      continue;
    }

    if (S_ISDIR(sStat.st_mode) != 0) {

      if (output_level >= DEBUG) {
        printf("File %s - is a directory\n", pDe->d_name);
      }

      if (recurse) {
        insertNode(tName, 0, 0, 0, subDirs);
      }
      continue;
    }

    /**
     | If the file has an extension (the rightmost dot followed by at
     | least one character), and if that extension matches one of the
     | entries in teXTree[i].extension: stores the file name (with the
     | extension stripped) in the appropriate linked list, together with
     | its modification time.
    **/

    if ((pFe = strrchr(pDe->d_name, '.')) != 0) {
      size_t nameLen;

      nameLen = pFe - pDe->d_name;
      if (nameLen < last) {
        Froot *pTT;

        if (output_level >= DEBUG) {
          printf("File %s - extension %s", pDe->d_name, pFe);
        }

        /**
         | Loop on recognized TeX-related file extensions
        **/

        for (pTT = teXTree;   pTT->extension != 0;   pTT++) {
          if (strcmp(pFe, pTT->extension) == 0) {
            insertNode(pDe->d_name, nameLen, sStat.st_mtime, access(tName, W_OK), pTT);

            if (output_level >= DEBUG) {
              printf(" - inserted in tree");
            }
            break;
          }
        } /* loop on known extensions */

        if (output_level >= DEBUG) {
          puts("");
        }

      } else {
        if (output_level >= VERBOSE) {
          printf("File %s - empty extension\n", pDe->d_name);
        }
      }

    } else {
      if (output_level >= DEBUG) {
        printf("File %s - without extension\n", pDe->d_name);
      }
    }
  }             /* while (readdir) ... */

  if (closedir(pDir) != 0) {
    fprintf(stderr, "Directory \"%s", dirName);
    perror("\"");
  }

  return teXTree;
}

static void printTree(
  Froot *teXTree
){

  /**
   | Prints all the file names archived in the linked lists (for
   | debugging purposes).
  **/

  if (output_level >= DEBUG) {
    Froot *pTT;           /* Running pointer over teXTree elements */

    puts("------------------------------Phase 2: tree printout");;
    for (pTT = teXTree;   pTT->extension != 0;   pTT++) {
      Fnode *pTeX;              /* Running pointer over TeX-related files */
      int    nNodes = 0;        /* Counter */

      for (pTeX = pTT->firstNode;   pTeX != 0;   pTeX = pTeX->next) {
        ++nNodes;
        printf("%s%s\n", pTeX->name, pTT->extension);
      }
      printf("  --> %d file%s with extension %s\n", nNodes,
             (nNodes == 1 ? "" : "s"), pTT->extension);
    }
  }
}

static void examineTree(
  Froot *teXTree,
  char  *dirName
){

  /**
   | Examines the linked lists for this directory doing the effective
   | cleanup.
  **/

  Froot *pTT;           /* Pointer over linked list trees      */
  Fnode *pTeX;          /* Running pointer over the .tex files */

  /**
   | Looks, for all the .tex files, if a corresponding entry with the same
   | name exists (with a different extension) in the other lists; if so,
   | and if its modification time is later than the one of the related
   | .tex file, removes it from the file system.
  **/
  putsMessage("------------------------------Phase 3: effective cleanup",
              DEBUG);

  for (pTeX = teXTree->firstNode;   pTeX != 0;   pTeX = pTeX->next) {
    char tName[FILENAME_MAX];

    sprintf(tName, "%s/%s.tex", dirName, pTeX->name);
    pTT = teXTree;

    if (output_level >= DEBUG) {
      printf("    Finding files related to %s:\n", tName);
    }

    for (pTT++;   pTT->extension != 0;   pTT++) {
      Fnode *pComp;

      for (pComp = pTT->firstNode;   pComp != 0;   pComp = pComp->next) {
        char cName[FILENAME_MAX];

        if (strcmp(pTeX->name, pComp->name) == 0) {
          sprintf(cName, "%s/%s%s", dirName, pTeX->name, pTT->extension);
          pComp->name[0] = '\0';

          /**
           | Remove generated file if more recent than source (default) or if
           | we permit the removal of files older than source
          **/
          if (difftime(pComp->mTime, pTeX->mTime) > 0.0 || older) {
            if (pComp->write == 0) {
              if (keep) {
                Froot *kExt;
                /**
                 | Loop on recognized TeX-related document extensions
                 | to make sure we aren't deleting a document we want
                 | to keep.
                 |
                 | Surely there's a more elegant way?
                **/
                int guard = 1;
                for (kExt = keepTree; kExt->extension != 0; kExt++) {
                  if ((strcmp(kExt->extension, pTT->extension) == 0)) {
                    guard = 0;
                    break;
                  }
                }
                if (guard) {
                  /**
                   | This is not a final TeX document. We can delete it
                  **/
                  nuke(cName);
                } else {
                  printf("*** %s not removed; keep is enabled ***\n", cName);
                }
              } else {
                /* We don't care to keep final documents */
                nuke(cName);
              }
            } else {
              if (output_level >= DEBUG) {
                printf("*** %s readonly; perms are %d***\n", cName,
                       pComp->write);
              }
              if (output_level >= VERBOSE) {
                printf("*** %s not removed; it is read only ***\n", cName);
              }
            }
          } else {
            if (output_level >= VERBOSE) {
              printf("*** %s not removed; %s is newer ***\n", cName, tName);
            }
          }
          break;
        }
      }
    }
  }

  /**
   | If some garbage file has not been deleted, list it
  **/

  putsMessage("------------------------------Phase 4: left garbage files",
              DEBUG);

  pTT = teXTree;
  for (pTT++;  pTT->extension != 0;  pTT++) {
    Fnode *pComp;

    for (pComp = pTT->firstNode;   pComp != 0;   pComp = pComp->next) {
      if (pComp->name[0] != '\0') {
        char cName[FILENAME_MAX];

        sprintf(cName, "%s/%s%s", dirName, pComp->name, pTT->extension);
        if (output_level >= VERBOSE) {
          printf("*** %s not removed; no .tex file found ***\n", cName);
        }
      }
    }
  }
}

static void releaseTree(
  Froot *teXTree
){

  /**
   | Cleanup of the file name storage structures: an _array_ of Froot's,
   | terminated by a NULL extension pointer as a sentinel, is assumed.
   | Every linked list nodes is freed; then releaseTree frees also the
   | root structure.
  **/

  Froot *pFR;

  putsMessage("------------------------------Phase 5: tree cleanup", DEBUG);

  for (pFR = teXTree;   pFR->extension != 0;   pFR++) {
    Fnode *pFN, *p;

    int nNodes = 0;
    if (output_level >= DEBUG) {
      printf("Dealing with extensions %s ...", pFR->extension);
    }

    for (pFN = pFR->firstNode;   pFN != 0;   pFN = p) {
      p = pFN->next;
      free(pFN);

      nNodes++;
    }

    if (output_level >= DEBUG) {
      printf("   %d nodes freed\n", nNodes);
    }
  }

  free(teXTree);
}

static void nuke(
  char *name
){

  /**
   | Removes "name" (the fully qualified file name) from the file system
  **/

  if ((output_level >= DEBUG) || pretend) {
    printf("*** File \"%s\" would have been removed ***\n", name);
  }

  if (pretend) {
    /* We don't need to continue if we aren't going to remove the file */
    return;
  }

  if (confirm) {
    char yn[LONG_ENOUGH], c;

    do {
      printf("Remove %s (y|n) ? ", name);
      if (fgets(yn, LONG_ENOUGH, stdin) == 0) return;
      if (yn[0] == '\0' || (c = tolower((unsigned char) yn[0])) == 'n') {
        return;
      }
    } while (c != 'y');
  }

  if (remove(name) != 0) {
    fprintf(stderr, "File \"%s", name);
    perror("\"");
  } else {
    if (output_level >= WHISPER) {
      printf("%s has been removed\n", name);
    }
  }

}

static char *baseName(
  char *pc
){
  char *p;

/**
 | Strips the (eventual) path information from the string pointed
 | to by 'pc'; if no file name is given, returns an empty string.
**/

  p = strrchr(pc, '/');
  if (p == 0) return pc;
  return ++p;
}

static void putsMessage(
  char *message,
  int  message_level
){
  if (message_level <= output_level) {
    puts(message);
  }
}

static void syntax()
{
  printf("lintex version %s\n", VERSION);
  puts("Usage:");
  printf("  %s [OPTIONS] [DIR [DIR ...]]\n", programName);
  puts("Purpose:");
  puts("  Removes unneeded TeX auxiliary files and editor backup files from"
       " the");
  puts("  given directories (default: the current directory); the TeX files"
       " are");
  puts("  actually removed only if their modification time is more recent"
       " than");
  puts("  the one of the related TeX source and if they aren't readonly.");
  puts("  Please see the manpage for a list of extensions that get removed.");
  puts("Options:");
  puts("  -i     : asks the user before removing any file;");
  puts("  -r     : scans recursively the subdirectories of the given");
  puts("           directories;");
  puts("  -b ext : \"ext\" is the trailing string identifying editor backup"
       " files");
  puts("           (defaults to \"~\").  -b \"\" avoids any cleanup of special");
  puts("           files;");
  puts("  -p     : pretend, show what files would be removed but don't actually");
  puts("           remove them;");
  puts("  -k     : keeps final document (.pdf, .ps, .dvi);");
  puts("  -o     : permit removal of files older than their sources;");
  puts("  -q     : quiet, only print error messages;");
  puts("  -v     : verbose, prints which files were removed and which weren't;");
  puts("  -d     : debug output, prints the answers to all of life's questions.");

  exit(EXIT_SUCCESS);
}
