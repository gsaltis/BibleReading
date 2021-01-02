CC		        = gcc
LINK			= gcc
CC_FLAGS		= -c -g -Wall -IGeneralUtilities -IRPiBaseModules
LINK_FLAGS		+= -g -LGeneralUtilities -LRPiBaseModules 

TARGET			= bible.exe
PLATFORM		= 
LIBS			+= -lutils -lsqlite3

OBJS			= $(sort				\
			    main.o				\
			   )
all			: $(TARGET)

$(TARGET)		: $(OBJS)
			  @echo [LD] $@
			  @$(LINK) $(LINK_FLAGS) -o $@ $(OBJS) $(LIBS)

%.o			: %.c
			  @echo [CC] $@
			  @$(CC) $(CC_FLAGS) $<

clean			:
			  rm -rf $(wildcard *~ *.o *.exe lib/*.o lib/*.a)

junkclean		:
			  rm -rf $(wildcard *~ WWW/*~ )

include			  depends.mk
