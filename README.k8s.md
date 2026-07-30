# TrapMaster — Kubernetes Deployment Guide

This guide walks an operator through deploying the TrapMaster API server and portal to a Kubernetes cluster using ArgoCD for GitOps continuous delivery.

---

## Architecture overview

```
GitHub Actions (CI)
  └─ builds Docker images on push to main / version tags
  └─ pushes to ghcr.io/OWNER/REPO/{api-server,portal}:<sha>

ArgoCD (GitOps)
  └─ watches k8s/overlays/production/ in this repo
  └─ syncs Deployments / Services / ConfigMaps automatically
  └─ ArgoCD Image Updater bumps image tags when new images arrive

Cluster layout (namespace: trapmaster)
  ├─ Deployment: api-server   (1 replica, port 8080)
  ├─ Service:    api-server   (ClusterIP :8080)
  ├─ Deployment: portal       (2 replicas, nginx port 80)
  └─ Service:    portal       (ClusterIP :80)
```

TLS is terminated by an external reverse proxy (Ingress / load balancer) in front of the cluster. The containers only speak HTTP.

---

## Prerequisites

| Tool | Version |
|------|---------|
| `kubectl` | ≥ 1.28 |
| `kustomize` | ≥ 5.0 (or `kubectl kustomize`) |
| ArgoCD | ≥ 2.9 |
| ArgoCD Image Updater | ≥ 0.14 (optional — for automatic tag bumps) |

You also need:
- A GitHub repository forked/cloned from this project
- Admin access to a Kubernetes cluster
- A PostgreSQL database reachable from the cluster (connection string ready)

---

## Step 1 — Replace placeholder values

Search for `OWNER/REPO` across the k8s and argocd directories and replace with your actual GitHub `<owner>/<repo>`:

```bash
# Preview all occurrences
grep -r "OWNER/REPO" k8s/ argocd/

# Replace (macOS / BSD sed needs '' after -i)
find k8s/ argocd/ -type f | xargs sed -i 's|OWNER/REPO|your-org/your-repo|g'
```

Commit and push the changes.

---

## Step 2 — Configure GitHub Actions secrets

GitHub Actions uses `GITHUB_TOKEN` (automatically available) to push images to `ghcr.io`. No additional secrets are needed for image publishing.

If your build needs additional environment variables (e.g. `VITE_API_URL` baked into the portal bundle), add them as **Actions repository secrets** and pass them as `build-args` in `.github/workflows/build-and-push.yml`.

---

## Step 3 — One-time cluster bootstrap

### 3a. Create the namespace

ArgoCD will create the namespace automatically on first sync (via `CreateNamespace=true`), but you can also do it manually:

```bash
kubectl apply -f k8s/base/namespace.yaml
```

### 3b. Create the API server Secret

The Secret is **not** managed by Kustomize or ArgoCD — you create it once and the cluster owns it. See `k8s/overlays/production/secrets.example.yaml` for the required keys.

```bash
kubectl create secret generic api-server-secrets \
  --namespace trapmaster \
  --from-literal=DATABASE_URL='postgres://user:pass@host:5432/dbname?sslmode=require' \
  --from-literal=JWT_SECRET='<random-256-bit-secret>' \
  --from-literal=SESSION_SECRET='<random-256-bit-secret>'
```

Generate secure random secrets:

```bash
node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"
```

---

## Step 4 — Install ArgoCD (if not already running)

```bash
kubectl create namespace argocd
kubectl apply -n argocd -f https://raw.githubusercontent.com/argoproj/argo-cd/stable/manifests/install.yaml
```

Wait for all pods to be ready:

```bash
kubectl wait --for=condition=available deployment --all -n argocd --timeout=120s
```

---

## Step 5 — Register the ArgoCD Application

```bash
kubectl apply -f argocd/application.yaml
```

ArgoCD will immediately begin syncing `k8s/overlays/production/` from the `main` branch. Watch progress:

```bash
# CLI
argocd app get trapmaster
argocd app sync trapmaster   # force sync if needed

# Or open the ArgoCD web UI and find the "trapmaster" application
```

---

## Step 6 — Verify the deployment

```bash
kubectl get pods -n trapmaster
kubectl logs -n trapmaster deployment/api-server
kubectl logs -n trapmaster deployment/portal

# Quick smoke test (from inside the cluster or via port-forward)
kubectl port-forward -n trapmaster svc/api-server 8080:8080
curl http://localhost:8080/api/health
```

---

## Continuous delivery flow

Once everything is running:

1. Push code to `main` → GitHub Actions builds and pushes new images tagged with the commit SHA and `latest`.
2. ArgoCD (or ArgoCD Image Updater) detects the new `latest` tag → updates the image in the Deployment → pods roll over automatically.
3. To pin a specific version, push a Git tag (`git tag v1.2.3 && git push --tags`) → the workflow also tags the image as `v1.2.3`.

---

## Updating the production overlay to a specific tag

Edit `k8s/overlays/production/kustomization.yaml` and change `newTag`:

```yaml
images:
  - name: ghcr.io/your-org/your-repo/api-server
    newName: ghcr.io/your-org/your-repo/api-server
    newTag: "abc1234def5678..."   # full SHA or version tag
```

Commit and push — ArgoCD syncs the change automatically.

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Pod `CrashLoopBackOff` | `kubectl logs -n trapmaster <pod>` — likely a missing env var or unreachable database |
| `ImagePullBackOff` | Ensure the image was pushed and `ghcr.io` is accessible; for private repos add an `imagePullSecret` |
| API returns 500 | Check `DATABASE_URL` in the Secret is correct and the DB is reachable from the cluster |
| Portal shows blank page | Ensure `BASE_PATH` was set correctly at build time in the Dockerfile `ARG` |
| ArgoCD out of sync | Run `argocd app sync trapmaster` or click Sync in the UI |
