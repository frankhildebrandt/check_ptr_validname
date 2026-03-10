import { defineCollection } from "astro:content";
import { z } from "astro:schema";

const projects = defineCollection({
  type: "data",
  schema: z.object({
    title: z.string(),
    subtitle: z.string(),
    focus: z.string(),
    deliverable: z.string(),
    repoUrl: z.string(),
    logo: z.optional(z.string()),
  }),
});

const downloadsIndividual = z.object({
  value: z.number(),
  unit: z.string(),
  title: z.string(),
  by: z.optional(z.number()),
});

const stats = defineCollection({
  type: "data",
  schema: z.array(downloadsIndividual),
});

export const collections = {
  projects,
  stats,
};
