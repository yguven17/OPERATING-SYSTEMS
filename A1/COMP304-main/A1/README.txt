COMP 304 - Operating Systems: Assignment 1

If you enter the following commands in the terminal in order, you will see the same results as the screenshots.

Problem 1: Forks

	Part (a)
		gcc A1Problem1PartA.c -o A1Problem1PartA
		./A1Problem1PartA 2
		./A1Problem1PartA 3
		./A1Problem1PartA 5

	Part (b)
		gcc A1Problem1PartB.c -o A1Problem1PartB
		./A1Problem1PartB &
		ps -l
	
Problem 2: Pipes
	
	gcc A1Problem2.c -o A1Problem2
	./A1Problem2 5 ls -la
	
Problem 3: Shared Memory

	gcc A1Problem3.c -o A1Problem3
	shuf -i 1-1000 | ./A1Problem3 98 3
