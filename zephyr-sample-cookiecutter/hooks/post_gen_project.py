#!/usr/bin/env python3

print(f"Successfully created a new Zephyr sample project: {{ cookiecutter.project_name }}")
print("\nTo build and flash this sample, navigate to the project directory and run:")
print("west build -b <your_board>")
print("west flash")