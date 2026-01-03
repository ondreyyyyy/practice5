FROM gcc:latest

WORKDIR /app

COPY *.cpp *.h *.json ./
COPY ./include/ ./include/

RUN g++ -std=c++17 -pthread -I. -I./include *.cpp -o dbserver

EXPOSE 7432

CMD ["./dbserver"]