from project import EclipseProject
import logging


def main():
    logging.basicConfig(level=logging.INFO)
    log = logging.getLogger("DBT")
    log.info("Starting...")
    eclipse = EclipseProject(log)
    log.info("Finishing.")


if __name__ == "__main__":
    main()
