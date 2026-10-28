# 🏥 Hospital Patient Management System (C & MySQL)

A C-based Hospital Patient Management System integrated with **MySQL** for persistent storage and built using custom core **Data Structures**.

## 🛠️ Data Structures Used
- **Doubly Linked List**: Maintains in-memory patient records for quick linear searching, updates, and deletion.
- **FIFO Queue**: Manages regular waiting patients (Emergency Levels 3 & 4).
- **Priority Queue**: Manages critical patients (Emergency Levels 1 & 2), automatically ordered by urgency level.
- **Sorting Algorithms**: Built-in implementations of **Bubble Sort** and **Selection Sort** by ID, Name, Age, and Emergency Level.

## 🚀 Setup & Compilation

1. **Import Database Schema**:
   ```bash
   mysql -u root -p < schema.sql