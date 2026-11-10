// Railway settings of the bagi-mahjong service, the web build of the game (Infrastructure as Code; Railway reads
// no railway.json after 2026-12-01). The partial "mahjong-web" owns only this service: apply leaves todo-api, Vikunja
// and Postgres of the same project alone. Variables (BASIC_AUTH_*) stay unmanaged: `railway variables`, never here.
//   cd web && npm install && railway config plan && railway config apply
// The deploy itself: README.md, "Deploy to Railway" (engine/tools/web/build_and_deploy.py --deploy).
import { defineRailway, project, service } from "railway/iac";

export const partial = "mahjong-web";

export default defineRailway(() => {
  const web = service("bagi-mahjong", {
    build: { builder: "DOCKERFILE", dockerfilePath: "Dockerfile" },
    deploy: {
      restartPolicyType: "ON_FAILURE",
      restartPolicyMaxRetries: 5,
      limitOverride: { containers: { cpu: 4, memoryBytes: 3000000000 } },
    },
    healthcheck: "/healthz",
    healthcheckTimeout: 60,
    replicas: { sfo: 1 },
  });

  return project("vikunja-bagi-engine", {
    variables: { managed: false },
    resources: [web],
  });
});
