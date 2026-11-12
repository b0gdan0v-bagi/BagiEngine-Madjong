// Railway settings of the bagi-mahjong-rooms service, the room server of the game (BagiMahjongServer; Infrastructure
// as Code; Railway reads no railway.json after 2026-12-01). The partial "mahjong-rooms" owns only this service: apply
// leaves mahjong-web, todo-api, Vikunja and Postgres of the same project alone. Variables stay unmanaged: they are set
// with `railway variables` (BAGI_ROOMS_ORIGINS above all, BAGI_ROOMS_SHUTDOWN_GRACE_MS), never here; docs/web.md has
// the list and the order.
//   cd tools/server && npm install && railway config plan && railway config apply
// The deploy itself: docs/web.md, "The room server on Railway" (engine/tools/rooms/build_and_deploy.py --deploy).
import { defineRailway, project, service } from "railway/iac";

export const partial = "mahjong-rooms";

// Players are in Europe: Amsterdam. The id is Railway's own and the SDK types do not check it (the web partial uses the
// short "sfo"); `railway config plan` does, and a wrong id fails there with nothing applied.
const kRegion = "europe-west4-drams3a";
// Exactly one replica: the rooms live in the memory of the process, a second replica would split them in two servers
// that do not know each other's room codes.
const kReplicas = 1;
// Small limits: a room is a few kilobytes and a hand a few messages; the default of the project would pay for more.
const kCpu = 1;
const kMemoryBytes = 536870912; // 512 MiB

export default defineRailway(() => {
  const rooms = service("bagi-mahjong-rooms", {
    build: { builder: "DOCKERFILE", dockerfilePath: "Dockerfile" },
    deploy: {
      restartPolicyType: "ON_FAILURE",
      restartPolicyMaxRetries: 5,
      limitOverride: { containers: { cpu: kCpu, memoryBytes: kMemoryBytes } },
    },
    healthcheck: "/healthz",
    healthcheckTimeout: 60,
    replicas: { [kRegion]: kReplicas },
  });

  return project("vikunja-bagi-engine", {
    variables: { managed: false },
    resources: [rooms],
  });
});
