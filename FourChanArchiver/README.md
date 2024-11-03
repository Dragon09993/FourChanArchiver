# FourChanArchiver

FourChanArchiver is a GTK-based application for browsing and managing 4chan threads from a specified board. The app allows users to view thread titles stored in Redis, manage thread data, and perform actions such as adding or deleting threads and generating audio summaries, all via interaction with a Python-based 4chan scraper backend.

## Features

- Display a list of 4chan thread titles from a specific board, retrieved from Redis.
- Add new threads by thread ID, delete threads, and update thread data.
- Generate audio summaries for individual threads.
- Customize Redis connection settings and board via the GUI.

## Requirements

To build and run FourChanArchiver, you will need:

- **GTK+ 3** - For the GUI.
- **Hiredis** - Redis C client library.
- **Python 3** - For backend thread management.
- **Docker** - To run the backend scraper in a container.
- **GCC** - To compile the application.

### Dependencies

The following packages are required for installation on Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential libgtk-3-dev libhiredis-dev python3 docker.io
```

Ensure that you have the `FourChanScraper` Python application running in Docker, with a container name or alias `4chan_scraper-scraper-1`.

## Build Instructions

1. **Clone the Repository**

    ```bash
    git clone <your-repository-url>
    cd FourChanArchiver
    ```

2. **Compile the Application**

    Use the provided `Makefile` to build the app:

    ```bash
    make
    ```

    This will create an executable named `FourChanArchiver` in the project directory.

3. **Configuration**

   The application requires a configuration file named `config.ini` in the same directory as the executable. The `config.ini` should contain the Redis connection settings and the 4chan board name:

   ```ini
   [redis]
   host=localhost
   port=6379

   [settings]
   board=board_name_here
   ```

4. **Running the Application**

   Run the application from the project directory with:

   ```bash
   ./FourChanArchiver
   ```

5. **Interacting with the GUI**

   - **Show Settings**: Configure Redis host, port, and board name.
   - **Add Thread**: Add a new thread by entering a thread ID. This communicates with the scraper backend to pull thread data.
   - **Delete Thread**: Deletes the selected thread from Redis.
   - **Refresh**: Refreshes the list of threads from Redis.
   - **Generate Audio**: Generates an audio summary for the selected thread.

## Docker Configuration

The application expects the Python scraper app to be running as a Docker container. Ensure Docker is installed and start the scraper container using the following command:

```bash
docker compose up -d
```

The Docker container should be accessible with the name `4chan_scraper-scraper-1` for the backend commands to function correctly.

---

## License

This project is licensed under the MIT License.

