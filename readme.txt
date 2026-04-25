Código específico para sistemas POSIX (Linux)
Testado em ambiente Ubuntu através do WSL no Windows


Compilar e executar servidor.c com:
    gcc -o ./out/servidor servidor.c tpool.c sgbd.c
    ./out/servidor

Compilar e executar cliente.c com:
    gcc -o ./out/cliente cliente.c
    ./out/cliente



Comandos para teste:

INSERT id = 1, name = "Arthur Felipe", age = 20
INSERT id = 2, name = "Ronaldo", age = 22
INSERT id = 3, name = "Emerson, Rocha", age = 24

SELECT id = 3
UPDATE id = 3, name = "Arthur, Silva", age = 21
DELETE id = 3
INSERT id = 3, name = "Arthur Silva", age = 20

UPDATE id = 1, name = "Arthur Felipe", age = 20
UPDATE id = 2, name = "Ronaldo da Silva", age = 32
UPDATE id = 3, name = "Emerson Rocha", age = 57