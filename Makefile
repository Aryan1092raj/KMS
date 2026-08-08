.PHONY: dev backend frontend test migrate

dev:
	docker-compose up

backend:
	cd backend && .venv/bin/uvicorn app.main:app --reload --host 0.0.0.0 --port 8000

frontend:
	cd frontend && npm run dev

test:
	cd backend && python -m pytest tests/ -v

test-unit:
	cd backend && python -m pytest tests/test_unit.py -v

migrate:
	cd backend && alembic upgrade head

migrate-new:
	cd backend && alembic revision --autogenerate -m "$(MSG)"

install-backend:
	cd backend && pip install -r requirements.txt

install-frontend:
	cd frontend && npm install

setup: install-backend install-frontend
	cp backend/.env.example backend/.env
	@echo "✅ Setup complete! Edit backend/.env with your credentials, then run 'make dev'"
