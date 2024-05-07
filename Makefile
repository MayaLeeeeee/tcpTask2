#Reference
#http://makepp.sourceforge.net/1.19/makepp_tutorial.html

CC = gcc -c
SHELL = /bin/bash

# compiling flags here
CFLAGS = -Wall -I.
LDFLAGS = -lm

LINKER = gcc -o
# linking flags here
LFLAGS   = -Wall

OBJDIR = ./obj

CLIENT_OBJECTS := sender/rdt_sender.o $(OBJDIR)/common.o $(OBJDIR)/packet.o
SERVER_OBJECTS := receiver/rdt_receiver.o $(OBJDIR)/common.o $(OBJDIR)/packet.o

#Program name
CLIENT := rdt_sender
SERVER := rdt_receiver

rm       = rm -f
rmdir    = rmdir 

TARGET:	$(OBJDIR) $(CLIENT)	$(SERVER)

$(CLIENT):	$(CLIENT_OBJECTS)
	$(LINKER)  $@  $(CLIENT_OBJECTS) $(LDFLAGS)
	@echo "Link complete!"

$(SERVER): $(SERVER_OBJECTS)
	$(LINKER)  $@  $(SERVER_OBJECTS)
	@echo "Link complete!"

$(OBJDIR)/%.o:	%.c obj/common.h obj/packet.h
	$(CC) $(CFLAGS)  $< -o $@
	@echo "Compilation complete!"

clean:
	${RM} receiver//rdt_receiver.o
	${RM} sender/rdt_sender.o
	${RM} ${OBJDIR}/packet.o
	${RM} ${OBJDIR}/common.o
	@echo "Cleanup complete!"

fclean:	clean
	${RM} ${CLIENT}
	${RM} ${SERVER}

$(OBJDIR):
	@[ -a $(OBJDIR) ]  || mkdir $(OBJDIR)
