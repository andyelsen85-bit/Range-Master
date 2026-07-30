# TrapMaster — Kubernetes Deployment Guide

## Architecture

```
Internet → Reverse Proxy (HTTPS) → K8s Ingress → portal (nginx:80) 
                                               → api-server (node:8080) → PostgreSQL
```

GitHub Actions builds Docker images on every push to `main` and pushes them to GitHub Container Registry (`ghcr.io`). ArgoCD watches the Git repo and syncs the cluster automatically.

---

## Prerequisites

| Tool | Version |
|------|---------|
| kubectl | 1.28+ |
| ArgoCD | 2.9+ |
| ArgoCD Image Updater | 0.12+ (optional, for auto tag updates) |
| PostgreSQL | 15+ (external or operator-managed) |

---

## Step 1 — Fork / push the repo to GitHub

```bash
git remote add origin https://github.com/OWNER/REPO.git
git push -u origin main
```

Replace every `OWNER/REPO` placeholder in the files below:
- `k8s/base/api-server/deployment.yaml`
- `k8s/base/portal/deployment.yaml`
- `k8s/overlays/production/kustomization.yaml`
- `argocd/application.yaml`

---

## Step 2 — Configure GitHub Actions

GitHub Actions uses `GITHUB_TOKEN` (automatically available) to push images to `ghcr.io`. No extra secrets are needed for the registry.

If you want the API server to embed environment variables at **build time** (e.g. a public API base URL), add them as GitHub Actions secrets and reference them in the workflow's `build-args`.

---

## Step 3 — Create the Kubernetes Secret

```bash
kubectl create namespace trapmaster

kubectl create secret generic trapmaster-secrets \
  --namespace trapmaster \
  --from-literal=database-url='postgres://user:pass@your-pg-host:5432/trapmaster' \
  --from-literal=jwt-secret="$(openssl rand -hex 32)" \
  --from-literal=session-secret="$(openssl rand -hex 32)"
```

---

## Step 4 — Run the database migration

The first time you deploy, apply the schema to your PostgreSQL instance:

```bash
# From your local machine with DATABASE_URL set
pnpm --filter @workspace/db run migrate
```

Or execute the SQL from `lib/db/migrations/` directly against your production database.

---

## Step 5 — Update the Ingress host

Edit `k8s/overlays/production/ingress.yaml` and replace `trapmaster.example.com` with your actual domain. Point your reverse proxy to the cluster's ingress IP / NodePort.

---

## Step 6 — Install ArgoCD Application

```bash
# Install ArgoCD if not already present
kubectl apply -n argocd -f https://raw.githubusercontent.com/argoproj/argo-cd/stable/manifests/install.yaml

# Create the Application
kubectl apply -f argocd/application.yaml
```

ArgoCD will now:
1. Clone the repo
2. Apply `k8s/overlays/production/` via Kustomize
3. Keep the cluster in sync with Git

---

## CI/CD Flow (after initial setup)

```
git push main
  → GitHub Actions builds api-server + portal images
  → Images pushed to ghcr.io/OWNER/REPO/{api-server,portal}:sha-<hash>
  → ArgoCD Image Updater detects new digest
  → ArgoCD updates the deployment and rolls out new pods
```

No manual `kubectl` commands needed after initial setup.

---

## Image tags

| Tag | When | Use |
|-----|------|-----|
| `sha-<short>` | Every push | Debugging, rollback |
| `sha-<full>` | Every push | ArgoCD Image Updater pinning |
| `latest` | Push to `main` | Convenience |
| `v1.2.3` | Git tag `v1.2.3` | Production releases |

---

## Rollback

```bash
# List recent ReplicaSets
kubectl rollout history deployment/api-server -n trapmaster

# Roll back one version
kubectl rollout undo deployment/api-server -n trapmaster
```

Or revert the Git commit and let ArgoCD self-heal.
