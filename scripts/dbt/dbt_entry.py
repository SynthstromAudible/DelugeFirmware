from project import E2Project
import logging


def main():
    logging.basicConfig(level=logging.DEBUG)
    log = logging.getLogger("DBT")
    log.info("Starting...")
    eclipse = E2Project(log)
    log.info("Finishing.")


if __name__ == "__main__":
    main()
