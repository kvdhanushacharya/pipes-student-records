# Student Record Processor: Pipe IPC in C

This Windows C program starts two processes:

- The **parent** asks for a student's name and Math, Science, and English marks. It sends each record through an anonymous pipe, prints the result, and displays a class summary at the end.
- The **child** receives each record through its standard input (`fd 0` equivalent), calculates the average, letter grade, and pass/fail status, then sends the result through its standard output (`fd 1` equivalent). A second pipe carries the reply back to the parent.

A student passes only if every subject mark is at least 40. Letter grades use the average: A >= 90, B >= 80, C >= 70, D >= 60, E >= 50, otherwise F.

## Build and run in VS Code

- Press **Ctrl+Shift+B** to build `student_records.exe` with GCC.
- Use **Tasks: Run Task** from the Command Palette and select **Run student record processor** to build and run it.
- Type `done` at the student-name prompt to finish entering records and receive the class summary.

GCC must be available on PATH. The project uses Windows anonymous pipes and `CreateProcess`.
