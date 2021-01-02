/*****************************************************************************
 * FILE NAME    : main.c
 * DATE         : January 11 2020
 * PROJECT      : 
 * COPYRIGHT    : Copyright (C) 2020 by Gregory R Saltis
 *****************************************************************************/

/*****************************************************************************!
 * Global Headers
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/*****************************************************************************!
 * Local Headers
 *****************************************************************************/
#include "GeneralUtilities/StringUtil.h"
#include "RPIBaseModules/sqlite3.h"
#include "GeneralUtilities/MemoryManager.h"

/*****************************************************************************!
 * Local Macros
 *****************************************************************************/
#define DATABASE_FILENAME               "bible-sqlite.db"
#define VERSES_BY_BOOK_QUERY_STRING             \
  "SELECT books.name, c, COUNT(v), b "          \
  "FROM t_asv "                                 \
  "JOIN books ON b == books.canonical "         \
  "GROUP BY b, c "                              \
  "ORDER BY books.chronological, c;"

#define VERSES_QUERY_STRING                     \
  "SELECT books.name, c, v, b "                 \
  "FROM t_asv "                                 \
  "JOIN books ON b == books.canonical "         \
  "ORDER BY books.chronological, c;"

#define VERSE_QUERY_RANGE                       \
  "SELECT books.name, c, v, b, t "              \
  "FROM t_asv "                                 \
  "JOIN books ON b == books.canonical "         \
  "WHERE id >= %d AND id <= %d "                \
  "ORDER BY books.chronological, c;"            \
  
#define SECONDS_IN_DAY                  86400

/******************************************************************************!
 * Local Type : ChapterCount
 ******************************************************************************/
struct _ChapterCount
{
  string                                bookName;
  int                                   chapter;
  int                                   verseCount;
  struct _ChapterCount*                 next;
};
typedef struct _ChapterCount ChapterCount;

/******************************************************************************!
 * Function : ReadScheduleEntry
 ******************************************************************************/
struct _ReadScheduleEntry
{
  string                                startBook;
  int                                   startBookIndex;
  int                                   startChapter;
  int                                   startVerse;
  string                                endBook;
  int                                   endBookIndex;
  int                                   endChapter;
  int                                   endVerse;
};
typedef struct _ReadScheduleEntry ReadScheduleEntry;

/*****************************************************************************!
 * Local Data
 *****************************************************************************/
sqlite3*
mainDatabase;

ChapterCount*
mainBibleChapters = NULL;

int*
mainDailyVerseCount;

int
mainRemainingDays;

string
mainMonthNames[12] =
{
 "January", "February", "March", "April", "May", "June",
 "July", "August", "September", "October", "November", "December"
};

time_t
mainToday = 0;

bool
mainReadToday = false;

bool
mainDisplayReadingSchedule = false;

ReadScheduleEntry**
mainReadingSchedule;

string
mainUserStartDate = NULL;

string
mainUserReadingDate = NULL;

/*****************************************************************************!
 * Local Functions
 *****************************************************************************/
int
GetTotalVersesCount
();

int
GetNumberofDaysRemaining
();

void
CreateReadingSchedule
();

time_t
GetStartDate
();

int
GetElapsedDays
(time_t InStartDate, time_t InEndDate);

void
Initialize
();

void
ProcessCommandLine
(int argc, char** argv);

void
DisplayHelp
();

void
ReadTodaysVerses
();

ReadScheduleEntry*
ReadScheduleEntryCreate
(string InStartBook, int InStartBookIndex, int InStartChapter, int InStartVerse, string InEndBook, int InEndBookIndex, int InEndChapter, int InEndVerse);

void
DisplayReadingSchdule
();

time_t
GetReadingDate
();

time_t
ParseDate
(string InDateString);

/******************************************************************************!
 * Function : main
 ******************************************************************************/
int main(int argc, char** argv)
{
  double                                versePerDay;
  int                                   errorcode;
  string                                s;
  int                                   totalVerses;
  double                                totalVersesRead, f;
  int                                   totalVersesReadI, i, j, k;

  Initialize();
  ProcessCommandLine(argc, argv);
  
  errorcode = sqlite3_open_v2(DATABASE_FILENAME, &mainDatabase,
                              SQLITE_OPEN_READWRITE, NULL);
  if ( errorcode != SQLITE_OK ) {
    s = (char*)sqlite3_errstr(errorcode);
    fprintf(stderr, "Error opening database %s : %s\n", DATABASE_FILENAME, s);
    return EXIT_FAILURE;
  }

  mainRemainingDays = GetNumberofDaysRemaining();
  totalVerses = GetTotalVersesCount();
  mainReadingSchedule = (ReadScheduleEntry**)GetMemory(mainRemainingDays * sizeof(ReadScheduleEntry*));
  versePerDay = (double)totalVerses / mainRemainingDays;

  totalVersesRead = 0;
  totalVersesReadI = 0;

  mainDailyVerseCount = (int*)GetMemory(sizeof(int) * mainRemainingDays);


  k = 0;
  for ( j = 0; j < mainRemainingDays; j++) {
    f = totalVersesRead + versePerDay;
    i = (int)f - totalVersesReadI;
    totalVersesReadI += i;
    totalVersesRead += versePerDay;
    if ( j + 1 < mainRemainingDays ) {
      k += i;
    }
    mainDailyVerseCount[j] = i;
  }

  mainDailyVerseCount[mainRemainingDays-1] = totalVerses - k;
  CreateReadingSchedule();

  if ( mainDisplayReadingSchedule ) {
    DisplayReadingSchdule();
  } else if ( mainReadToday ) {
    ReadTodaysVerses();
  }
  return EXIT_SUCCESS;
}

/******************************************************************************!
 * Function : GetTotalVersesCount
 ******************************************************************************/
int
GetTotalVersesCount()
{
  int                                   verseCount, chapter;
  string                                bookname;
  int                                   totalVerses = 0;
  sqlite3_stmt*                         statement;
  string                                selectString = VERSES_BY_BOOK_QUERY_STRING;
  ChapterCount*                         bookChapter;
  ChapterCount*                         last = NULL;
  
  if ( SQLITE_OK != sqlite3_prepare_v2(mainDatabase, selectString, strlen(selectString), &statement, NULL) ) {
    return 0 ;

  }
  
  if ( SQLITE_ROW != sqlite3_step(statement) ) {
    sqlite3_finalize(statement);
    return 0;
  }

  do {
    bookname = (string)sqlite3_column_text(statement, 0);
    chapter = sqlite3_column_int(statement, 1);
    verseCount = sqlite3_column_int(statement, 2);
    bookChapter = (ChapterCount*)GetMemory(sizeof(ChapterCount));
    bookChapter->bookName = StringCopy(bookname);
    bookChapter->chapter = chapter;
    bookChapter->verseCount = verseCount;
    bookChapter->next = NULL;
    
    if ( last == NULL ) {
      mainBibleChapters = bookChapter;
    } else {
      last->next = bookChapter;
    }
    last = bookChapter;
    
    totalVerses += verseCount;
  } while ( SQLITE_ROW == sqlite3_step(statement) );

  return totalVerses;
}

/******************************************************************************!
 * Function : GetNumberofDaysRemaining();
 ******************************************************************************/
int
GetNumberofDaysRemaining()
{
  int                                   days;
  time_t                                secs, endOfYear;
  struct tm*                            ts;
  double                                elapsedSeconds;
  int                                   elapsedDays;

  mainToday = GetStartDate();
  days = mainToday / SECONDS_IN_DAY;

  secs = (days) * SECONDS_IN_DAY;

  ts = localtime(&secs);

  ts->tm_mon = 11;
  ts->tm_mday = 31;
  
  endOfYear = mktime(ts);

  elapsedSeconds = difftime(endOfYear, secs);
  elapsedDays = (int)(elapsedSeconds / SECONDS_IN_DAY);
  
  return elapsedDays;
}

/******************************************************************************!
 * Function : CreateReadingSchedule
 ******************************************************************************/
void
CreateReadingSchedule
()
{
  int                                   i, n, j, total;
  string                                startBook, endBook;
  int                                   startBookIndex, endBookIndex;
  int                                   startChapter, startVerse, endChapter, endVerse;
  sqlite3_stmt*                         statement;
  string                                selectString = VERSES_QUERY_STRING;
  int                                   days;
  time_t                                secs;
  
  days = (mainToday / SECONDS_IN_DAY) + 1;
  secs = (days) * SECONDS_IN_DAY;
  
  if ( SQLITE_OK != sqlite3_prepare_v2(mainDatabase, selectString, strlen(selectString), &statement, NULL) ) {
    return;
  }

  total = 0;
  for (i = 0; i < mainRemainingDays; i++) {
    n = mainDailyVerseCount[i];
    total += n;
    if ( SQLITE_ROW != sqlite3_step(statement) ) {
      sqlite3_finalize(statement);
      return;
    }
    startBook = StringCopy((string)sqlite3_column_text(statement, 0));
    startChapter = sqlite3_column_int(statement, 1);
    startVerse = sqlite3_column_int(statement, 2);
    startBookIndex = sqlite3_column_int(statement, 3);
    for (j = 1; j < n-1; j++)
      if ( SQLITE_ROW != sqlite3_step(statement) ) {
        sqlite3_finalize(statement);
        return;
      }
    if ( SQLITE_ROW != sqlite3_step(statement) ) {
      sqlite3_finalize(statement);
      return;
    }
    endBook= StringCopy((string)sqlite3_column_text(statement, 0));
    endChapter = sqlite3_column_int(statement, 1);
    endVerse = sqlite3_column_int(statement, 2);
    endBookIndex = sqlite3_column_int(statement, 3);

    mainReadingSchedule[i] = ReadScheduleEntryCreate(startBook, startBookIndex, startChapter, startVerse,
                                                     endBook, endBookIndex, endChapter, endVerse);
    FreeMemory(startBook);
    FreeMemory(endBook);
    secs += SECONDS_IN_DAY;
  }
  sqlite3_finalize(statement);
}

/******************************************************************************!
 * Function : GetStartDate
 ******************************************************************************/
time_t
GetStartDate
()
{
  string                                startDate;

  if ( mainUserStartDate ) {
    startDate = mainUserStartDate;
  } else {
    startDate = getenv("BibleStartDate");
  }
  if ( NULL == startDate ) {
    return time(NULL);
  }

  return ParseDate(startDate);
}

/*****************************************************************************!
 * Function : ParseDate
 *****************************************************************************/
time_t
ParseDate
(string InDateString)
{
  int                                   day;
  int                                   year;
  int                                   month;
  struct tm                             ts;
  StringList*                           dateParts;

  dateParts = StringSplit(InDateString, "/", false);
  if ( NULL == dateParts ) {
    return (time_t)0;
  }
  if ( dateParts->stringCount != 3 ) {
    StringListDestroy(dateParts);
    return (time_t)0;
  }

  memset(&ts, 0x00, sizeof(struct tm));

  year = atoi(dateParts->strings[2]);
  month = atoi(dateParts->strings[0]);
  day = atoi(dateParts->strings[1]);

  year -= 1900;
  month--;
  
  ts.tm_year = year;
  ts.tm_mon = month;
  ts.tm_mday = day;

  StringListDestroy(dateParts);
  return mktime(&ts);
}

/******************************************************************************!
 * Function : GetElapsedDays
 ******************************************************************************/
int
GetElapsedDays
(time_t InStartDate, time_t InEndDate)
{
  time_t                                t1, t2, t3;

  if ( InStartDate > InEndDate ) {
    return 0;
  }
  t1 = InStartDate / SECONDS_IN_DAY;
  t1 *= SECONDS_IN_DAY;

  t2 = InEndDate / SECONDS_IN_DAY;
  t2 *= SECONDS_IN_DAY;

  t3 = t2 - t1;
  return (int)(t3 / SECONDS_IN_DAY);
}

/******************************************************************************!
 * Function : Initialize
 ******************************************************************************/
void
Initialize
()
{
  mainReadToday = false;
  mainDisplayReadingSchedule = false;
  mainUserStartDate = NULL;
  mainUserReadingDate = NULL;
}

/******************************************************************************!
 * Function : ProcessCommandLine
 ******************************************************************************/
void
ProcessCommandLine
(int argc, char** argv)
{
  string                                command;
  int                                   i;
  
  if ( argc < 2 ) {
    return;
  }

  for ( i = 1 ; i < argc; i++ ) {
    command = argv[i];

    if ( StringEqualsOneOf(command, "-t", "--startdate", NULL ) ) {
      i++;
      if ( i + 1 == argc ) {
        fprintf(stderr, "%s requires a date\n", command);
        DisplayHelp();
        exit(EXIT_FAILURE);
      }
      if ( mainUserStartDate ) {
        FreeMemory(mainUserStartDate);
      }
      mainUserStartDate = StringCopy(argv[i]);
      continue;
    }
    if ( StringEqualsOneOf(command, "-d", "--date", NULL ) ) {
      i++;
      if ( i + 1 == argc ) {
        fprintf(stderr, "%s requires a date\n", command);
        DisplayHelp();
        exit(EXIT_FAILURE);
      }
      if ( mainUserReadingDate ) {
        FreeMemory(mainUserReadingDate);
      }
      mainUserReadingDate = StringCopy(argv[i]);
      continue;
    }
    if ( StringEqual(command, "-r") || StringEqual(command, "--read" ) ) {
      mainReadToday = true;
    } else if ( StringEqual(command, "-h") || StringEqual(command, "--help") ) {
      DisplayHelp();
      exit(EXIT_SUCCESS);
    } else if ( StringEqual(command, "-s") || StringEqual(command, "--schedule") ) {
      mainDisplayReadingSchedule = true;
    } else {
      fprintf(stderr, "Unknown ommmand %s\n", command);
      DisplayHelp();
      exit(EXIT_FAILURE);
    }
  }
}

/******************************************************************************!
 * Function : DisplayReadingSchdule
 ******************************************************************************/
void
DisplayReadingSchdule
()
{
  int                                   i;
  
  for ( i = 0; i < mainRemainingDays; i++) {
    printf("%20s %3d %3d  -- %s %3d %3d\n",
           mainReadingSchedule[i]->startBook,
           mainReadingSchedule[i]->startChapter,
           mainReadingSchedule[i]->startVerse,
           mainReadingSchedule[i]->endBook,
           mainReadingSchedule[i]->endChapter,
           mainReadingSchedule[i]->endVerse);
  }
}

/******************************************************************************!
 * Function : DisplayHelp
 ******************************************************************************/
void
DisplayHelp
()
{
  int                                   n;
  
  n = fprintf(stdout, "Usage bible : ");
  fprintf(stdout, "-r, --read   -h, --help\n");
  fprintf(stdout, "%*s-t, --startdate MM/DD/YYYY : Define the date from which the reading program starts\n", n, " ");
  fprintf(stdout, "%*s-d, --date MM/DD/YYYY      : Define the date for which the scripture is to be read\n", n, " ");
  fprintf(stdout, "%*s-h, --help                 : Display this information\n", n, " ");
  fprintf(stdout, "%*s-r, --read                 : Read today's scripture\n", n, " ");
  fprintf(stdout, "%*s-s, --schedule             : Read reading schedule\n", n, " ");
}

/******************************************************************************!
 * Function : ReadTodaysVerses
 ******************************************************************************/
void
ReadTodaysVerses
()
{
  string                                s2;
  time_t                                startDate, todaysDate;
  int                                   elapsedDays;
  ReadScheduleEntry*                    entry;
  int                                   startid, endid;
  sqlite3_stmt*                         statement;
  string                                selectString = VERSE_QUERY_RANGE;
  string                                bookName, text;
  int                                   chapter, verse;
  FILE*                                 outFile;
  
  todaysDate = GetReadingDate();
  startDate = GetStartDate();

  elapsedDays = GetElapsedDays(startDate, todaysDate);
  entry = mainReadingSchedule[elapsedDays];
  startid =
    entry->startBookIndex * 1000000 +
    entry->startChapter * 1000 +
    entry->startVerse;

  endid =
    entry->endBookIndex * 1000000 +
    entry->endChapter * 1000 +
    entry->endVerse;

  s2 = (string)GetMemory(strlen(selectString) + 24);
  sprintf(s2, selectString, startid, endid);

  if ( SQLITE_OK != sqlite3_prepare_v2(mainDatabase, s2, strlen(s2), &statement, NULL) ) {
    return;
  }

  if ( SQLITE_ROW != sqlite3_step(statement) ) {
    sqlite3_finalize(statement);
    return ;
  }

  outFile = fopen("today.html", "wb");
  fprintf(outFile, "<HTML>\n");
  fprintf(outFile, "<HEAD>\n");
  fprintf(outFile, "  <LINK href=\"style.css\" type=\"text/css\" rel=\"stylesheet\"></LINK>\n");
  fprintf(outFile, "</HEAD>\n");
  fprintf(outFile, "<BODY>\n");
  fprintf(outFile, "<TABLE>\n");
  do {
    bookName = (string)sqlite3_column_text(statement, 0);
    chapter  = sqlite3_column_int(statement, 1);
    verse = sqlite3_column_int(statement, 2);
    text = (string)sqlite3_column_text(statement, 4);
    fprintf(outFile, "<tr>\n");
    fprintf(outFile, "<td class=\"verse\">%s %d:%d</td>\n", bookName, chapter, verse);
    fprintf(outFile, "<td class=\"text\">%s</td>\n", text);
    fprintf(outFile, "</tr>\n");
  }  
  while ( SQLITE_ROW == sqlite3_step(statement) );    
  fprintf(outFile, "</TABLE>\n");
  fprintf(outFile, "</BODY>\n");
  fprintf(outFile, "</HTML>\n");
  fclose(outFile);
  sqlite3_finalize(statement);
}

/******************************************************************************!
 * Function : ReadScheduleEntryCreate
 ******************************************************************************/
ReadScheduleEntry*
ReadScheduleEntryCreate
(string InStartBook, int InStartBookIndex, int InStartChapter, int InStartVerse,
 string InEndBook, int InEndBookIndex, int InEndChapter, int InEndVerse)
{
  ReadScheduleEntry*                    entry;
  
  if ( NULL == InStartBook || NULL == InEndBook ) {
    return NULL;
  }

  entry = (ReadScheduleEntry*)GetMemory(sizeof(ReadScheduleEntry));
  entry->startBook = StringCopy(InStartBook);
  entry->startBookIndex = InStartBookIndex;
  entry->startChapter = InStartChapter;
  entry->startVerse = InStartVerse;

  entry->endBook = StringCopy(InEndBook);
  entry->endBookIndex = InEndBookIndex;
  entry->endChapter = InEndChapter;
  entry->endVerse = InEndVerse;

  return entry;
}

/*****************************************************************************!
 * Function : GetReadingDate
 *****************************************************************************/
time_t
GetReadingDate
()
{
  time_t                                t;
  
  if ( NULL == mainUserReadingDate ) {
    return time(NULL);
  }

  t = ParseDate(mainUserReadingDate);
  if ( t > 0 ) {
    return t;
  }

  return time(NULL);
}

  
