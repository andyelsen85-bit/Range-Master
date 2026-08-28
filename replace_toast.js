const fs = require("fs");

let file = fs.readFileSync("artifacts/portal/src/pages/admin-kredite.tsx", "utf-8");

file = file.replace(
  `onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["admin-kredite-dag", datum] });
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
    },`,
  `onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["admin-kredite-dag", datum] });
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
      toast({ title: "Späicheren erfollegräich", description: "D'Kreditter goufen ugepasst." });
    },`
);

file = file.replace(
  `onSuccess: () => {
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
    },`,
  `onSuccess: () => {
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
      toast({ title: "Späicheren erfollegräich", description: "D'Munitioun gouf ugepasst." });
    },`
);

fs.writeFileSync("artifacts/portal/src/pages/admin-kredite.tsx", file);
